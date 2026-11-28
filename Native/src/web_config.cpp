#include "web_config.hpp"

#include <algorithm>

bool validOtaUrl(std::string_view url)
{
    if (url.empty())
        return true;
    if (url.size() > otaUrlLimit || std::ranges::any_of(url, [](unsigned char ch) {
            return ch <= ' ' || ch >= 127 || ch == '\\';
        }))
        return false;
    const auto separator = url.find("://");
    if (separator == std::string_view::npos)
        return false;
    const auto scheme = url.substr(0, separator);
    const auto sameLetter = [](char lower, char actual) {
        return lower == actual || lower - 'a' + 'A' == actual;
    };
    if (!std::ranges::equal(std::string_view("https"), scheme, sameLetter) &&
        !std::ranges::equal(std::string_view("http"), scheme, sameLetter))
        return false;
    const auto endpoint = url.substr(separator + 3);
    const auto authority = endpoint.substr(0, endpoint.find_first_of("/?#"));
    const auto userInfo = authority.rfind('@');
    const auto host = userInfo == std::string_view::npos ? authority : authority.substr(userInfo + 1);
    return !host.empty() && host.front() != ':';
}

#if defined(DECKDS_WEB_CONFIG)
#include "web_password.hpp"

#include <arpa/inet.h>
#include <microhttpd.h>
#include <unistd.h>

#include <charconv>
#include <cctype>
#include <cmath>
#include <locale>
#include <mutex>
#include <new>
#include <sstream>
#include <string_view>
#include <utility>

namespace {
#include "web_assets.inc"

constexpr unsigned short port = 8080;
constexpr size_t requestLimit = 256;
constexpr size_t commandLimit = 24;
constexpr std::string_view otaUrlPath = "/api/ota/url";
constexpr std::string_view passwordPath = "/api/access/password";
constexpr std::string_view loginPath = "/api/access/login";
constexpr auto sessionCookie = "deckds_session";
constexpr std::array<std::string_view, static_cast<size_t>(WebSetting::Count)> settingNames{
    "theme", "brightness", "clock", "sfx", "lid", "accent", "sfxVolume", "sfxOutput"};
constexpr std::array<int, settingNames.size()> settingMax{2, 6, 1, 1, 1, 3, 2, 1};
constexpr std::array<std::string_view, 7> playbackNames{"previous", "play", "pause", "next", "seek", "volume", "mute"};

bool acceptsHost(MHD_Connection* connection, std::string_view host, std::string_view machineHost)
{
    if (!machineHost.empty() && host == machineHost)
        return true;
    const auto* info = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CONNECTION_FD);
    if (!info)
        return false;
    sockaddr_in local{};
    socklen_t size = sizeof(local);
    if (getsockname(info->connect_fd, reinterpret_cast<sockaddr*>(&local), &size) != 0 || local.sin_family != AF_INET)
        return false;
    if (ntohl(local.sin_addr.s_addr) == INADDR_LOOPBACK && host == "localhost:8080")
        return true;
    char ip[INET_ADDRSTRLEN]{};
    return inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip)) && host == std::string(ip) + ':' + std::to_string(port);
}

std::string jsonString(std::string_view value)
{
    constexpr std::string_view hex = "0123456789abcdef";
    std::string encoded = "\"";
    for (const unsigned char ch : value) {
        if (ch == '"' || ch == '\\')
            encoded += '\\';
        if (ch < 32) {
            encoded += "\\u00";
            encoded += hex[ch >> 4];
            encoded += hex[ch & 15];
        } else {
            encoded += static_cast<char>(ch);
        }
    }
    encoded += '"';
    return encoded;
}

void writeScan(std::ostream& output, const std::optional<WebConfigState::Scan>& scan)
{
    if (scan) {
        output << "{\"checked\":" << scan->checked << ",\"total\":" << scan->total << ",\"full\":" << scan->full << '}';
    } else {
        output << "null";
    }
}

void writeConnectivity(std::ostream& output, const WebConfigState& state);

std::string json(const WebConfigState& state)
{
    std::ostringstream output;
    output << std::boolalpha
           << "{\"folder\":" << jsonString(state.folder)
           << ",\"track\":" << jsonString(state.track) << ",\"artist\":" << jsonString(state.artist)
           << ",\"message\":" << jsonString(state.message)
           << ",\"tracks\":" << state.tracks << ",\"canScan\":" << state.canScan
           << ",\"missing\":" << state.missing << ",\"review\":" << state.review
           << ",\"removing\":" << state.removing << ",\"scan\":";
    writeScan(output, state.scan);
    output << ",\"settings\":{";
    for (size_t i = 0; i < settingNames.size(); ++i) {
        if (i != 0)
            output << ',';
        output << jsonString(settingNames[i]) << ':' << state.settings[i];
    }
    const auto& playback = state.playback;
    output << "},\"playback\":{\"trackId\":" << jsonString(playback.trackId)
           << ",\"positionMs\":" << playback.positionMs << ",\"durationMs\":" << playback.durationMs
           << ",\"volume\":" << playback.volume << ",\"available\":" << playback.available
           << ",\"playing\":" << playback.playing << ",\"muted\":" << playback.muted
           << ",\"canPlay\":" << playback.canPlay << ",\"canPrevious\":" << playback.canPrevious
           << ",\"canNext\":" << playback.canNext << "},\"ota\":{\"url\":" << jsonString(state.ota.url)
           << ",\"revision\":" << state.ota.revision << ",\"error\":" << jsonString(state.ota.error) << '}';
    writeConnectivity(output, state);
    output << '}';
    return std::move(output).str();
}

template<typename Range, typename Write>
void writeArray(std::ostream& output, const Range& values, Write write)
{
    output << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0)
            output << ',';
        write(values[i]);
    }
    output << ']';
}

void writeConnectivity(std::ostream& output, const WebConfigState& state)
{
    output << ",\"accents\":";
    writeArray(output, state.accents, [&](const auto name) { output << jsonString(name); });
    const auto& wifi = state.wifi;
    output << ",\"wifi\":{\"supported\":" << wifi.supported << ",\"busy\":" << wifi.busy
           << ",\"connected\":" << wifi.connected << ",\"error\":" << wifi.error
           << ",\"message\":" << jsonString(wifi.message) << ",\"accessPoint\":";
    if (wifi.accessPoint) {
        output << "{\"name\":" << jsonString(wifi.accessPoint->name) << ",\"strength\":";
        if (wifi.accessPoint->strength)
            output << *wifi.accessPoint->strength;
        else
            output << "null";
        output << '}';
    } else {
        output << "null";
    }
    output << ",\"networks\":";
    writeArray(output, wifi.networks, [&](const WifiNetwork& network) {
        constexpr std::array securityNames{"open", "personal", "sae", "other"};
        output << "{\"id\":" << jsonString(network.id) << ",\"name\":" << jsonString(network.name)
               << ",\"security\":" << jsonString(securityNames[static_cast<size_t>(network.security)])
               << ",\"strength\":" << network.strength << ",\"connected\":" << network.connected << '}';
    });
    const auto& bluetooth = state.bluetooth;
    output << "},\"bluetooth\":{\"supported\":" << bluetooth.supported << ",\"busy\":" << bluetooth.busy
           << ",\"scanning\":" << bluetooth.scanning << ",\"demo\":" << bluetooth.demo
           << ",\"error\":" << bluetooth.error << ",\"message\":" << jsonString(bluetooth.message)
           << ",\"devices\":";
    writeArray(output, bluetooth.devices, [&](const WebConfigState::Bluetooth::Device& device) {
        output << "{\"id\":" << jsonString(device.id) << ",\"name\":" << jsonString(device.name)
               << ",\"paired\":" << device.paired << ",\"connected\":" << device.connected << '}';
    });
    output << '}';
}

std::string debugJson(const WebDebugState& state)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    const auto writeRate = [&](int value) {
        if (value > 0)
            output << value;
        else
            output << "null";
    };
    output << std::boolalpha << "{\"schema\":1"
           << ",\"app\":{\"version\":" << jsonString(state.version) << ",\"sdlVersion\":" << jsonString(state.sdlVersion)
           << ",\"mode\":" << jsonString(state.mode) << ",\"uptimeMs\":" << state.uptimeMs << ",\"frameMs\":" << state.frameMs
           << "},\"system\":{\"platform\":" << jsonString(state.platform) << ",\"machine\":" << jsonString(state.machine)
           << ",\"kernel\":" << jsonString(state.kernel) << ",\"model\":" << jsonString(state.model)
           << ",\"logicalCpus\":" << state.logicalCpus << ",\"ramMiB\":" << state.ramMiB
           << ",\"peakRssBytes\":" << state.peakRssBytes << ",\"loadAverage1m\":";
    if (state.loadAverage && std::isfinite(*state.loadAverage))
        output << *state.loadAverage;
    else
        output << "null";
    output << ",\"powerState\":" << jsonString(state.powerState) << ",\"batteryPercent\":";
    if (state.batteryPercent)
        output << *state.batteryPercent;
    else
        output << "null";
    output << ",\"wifiConnected\":" << state.wifiConnected << ",\"bluetoothConnected\":" << state.bluetoothConnected
           << ",\"lidClosed\":" << state.lidClosed << ",\"displaysOff\":" << state.displaysOff
           << "},\"video\":{\"driver\":" << jsonString(state.videoDriver) << ",\"displays\":";
    writeArray(output, state.displays, [&](const auto& display) {
        output << "{\"name\":" << jsonString(display.name) << ",\"width\":" << display.width
               << ",\"height\":" << display.height << ",\"refreshHz\":";
        if (std::isfinite(display.refreshHz) && display.refreshHz > 0)
            output << display.refreshHz;
        else
            output << "null";
        output << '}';
    });
    output << ",\"windows\":";
    writeArray(output, state.windows, [&](const auto& window) {
        output << "{\"panel\":" << jsonString(window.panel) << ",\"renderer\":" << jsonString(window.renderer)
               << ",\"width\":" << window.width << ",\"height\":" << window.height << '}';
    });
    const auto& audio = state.audio;
    output << "},\"audio\":{\"driver\":" << jsonString(audio.driver) << ",\"codec\":" << jsonString(audio.codec)
           << ",\"sourceRate\":" << jsonString(audio.sourceRate) << ",\"playing\":" << audio.playing << ",\"decoderHz\":";
    writeRate(audio.decoderHz);
    output << ",\"deviceHz\":";
    writeRate(audio.deviceHz);
    output << ",\"deviceChannels\":";
    writeRate(audio.deviceChannels);
    output << ",\"deviceBufferFrames\":";
    writeRate(audio.deviceBufferFrames);
    output << "},\"input\":{\"touchDevices\":" << state.touchDevices << ",\"gamepads\":";
    writeArray(output, state.gamepads, [&](const auto& name) { output << jsonString(name); });
    output << ",\"lastControllerEvent\":";
    if (const auto& event = state.lastControllerEvent) {
        output << "{\"button\":" << jsonString(event->button) << ",\"pressed\":" << event->pressed
               << ",\"timeMs\":" << event->timeMs << '}';
    } else {
        output << "null";
    }
    output << "},\"library\":{\"tracks\":" << state.tracks << ",\"queue\":" << state.queue
           << ",\"missing\":" << state.missing << ",\"status\":" << jsonString(state.libraryStatus) << ",\"scan\":";
    writeScan(output, state.scan);
    output << "},\"logs\":";
    writeArray(output, state.logs, [&](const auto& log) {
        output << "{\"timeMs\":" << log.timeMs << ",\"category\":" << log.category << ",\"priority\":" << log.priority
               << ",\"message\":" << jsonString(log.message) << '}';
    });
    output << '}';
    return std::move(output).str();
}

template<typename Number>
std::optional<Number> number(std::string_view text)
{
    Number result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return result;
}

std::string_view header(MHD_Connection* connection, const char* name)
{
    const auto* value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, name);
    return value ? std::string_view(value) : std::string_view{};
}

MHD_Result respond(MHD_Connection* connection, unsigned status, std::string_view type, std::string_view body,
                   const char* extraHeader = nullptr, std::string_view extraValue = {})
{
    auto* response = MHD_create_response_from_buffer(body.size(), const_cast<char*>(body.data()), MHD_RESPMEM_MUST_COPY);
    if (!response)
        return MHD_NO;
    MHD_add_response_header(response, "Content-Type", type.data());
    MHD_add_response_header(response, "Cache-Control", "no-store");
    MHD_add_response_header(response, "X-Content-Type-Options", "nosniff");
    MHD_add_response_header(response, "Referrer-Policy", "no-referrer");
    if (extraHeader)
        MHD_add_response_header(response, extraHeader, extraValue.data());
    MHD_add_response_header(response, "Content-Security-Policy",
        "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; "
        "frame-ancestors 'none'; base-uri 'none'; form-action 'none'");
    const auto result = MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);
    return result;
}

MHD_Result errorResponse(MHD_Connection* connection, unsigned status, std::string_view message)
{
    return respond(connection, status, "application/json", "{\"error\":" + jsonString(message) + "}");
}

bool passwordText(std::string_view value)
{
    return value.size() <= WebPassword::limit && std::ranges::none_of(value, [](unsigned char character) {
        return character < 32 || character == 127;
    });
}

std::string rememberSession(std::string_view token)
{
    // Browsers cap persistent cookies. Renew the cookie each time a protected page opens.
    return std::string(sessionCookie) + '=' + std::string(token) +
           "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" + (token.empty() ? "0" : "34560000");
}

struct Request {
    std::string body;
    bool tooLarge = false;
};
}

struct WebConfig::Server {
    MHD_Daemon* daemon = nullptr;
    std::string machineHost, failure;
    std::mutex mutex;
    std::string state = json({});
    std::string debugState;
    std::vector<WebConfigCommand> commands;
    std::string playbackTrackId;
    bool canScan = false;
    uint64_t activeReview = 0;
    WebPassword password;
    bool wifiSupported = false, wifiBusy = false, bluetoothSupported = false, bluetoothBusy = false, bluetoothScanning = false;
    std::vector<WifiNetwork> wifiNetworks;
    std::vector<WebConfigState::Bluetooth::Device> bluetoothDevices;

    MHD_Result signIn(MHD_Connection* connection, std::string_view body)
    {
        if (!password.enabled())
            return respond(connection, 200, "application/json", "{}");
        if (!passwordText(body) || !password.matches(body))
            return errorResponse(connection, 401, "Incorrect password.");
        const auto token = password.newSession();
        if (token.empty())
            return errorResponse(connection, 500, "DeckDS could not start a session.");
        return respond(connection, 200, "application/json", "{}", "Set-Cookie", rememberSession(token));
    }

    MHD_Result accessPassword(MHD_Connection* connection, std::string_view method, std::string_view body)
    {
        if (method == "POST") {
            if (!passwordText(body))
                return errorResponse(connection, 400, "Use up to 128 bytes without control characters.");
            if (!password.save(body))
                return errorResponse(connection, 500, "DeckDS could not save the Web Config password.");
            const auto token = password.newSession();
            if (password.enabled() && token.empty())
                return errorResponse(connection, 500, "Password saved. Sign in again.");
            return respond(connection, 200, "application/json", password.enabled() ? "{\"enabled\":true}" : "{\"enabled\":false}",
                           "Set-Cookie", rememberSession(token));
        }
        return respond(connection, 200, "application/json", password.enabled() ? "{\"enabled\":true}" : "{\"enabled\":false}");
    }

    MHD_Result networkCommand(MHD_Connection* connection, std::string_view path, std::string_view body)
    {
        using Type = WebConfigCommand::Type;
        const bool wifi = path.starts_with("/api/wifi/");
        const auto action = path.substr(wifi ? std::string_view("/api/wifi/").size() :
                                              std::string_view("/api/bluetooth/").size());
        if (!(wifi ? wifiSupported : bluetoothSupported))
            return errorResponse(connection, 503, wifi ? "Wi-Fi setup is unavailable." : "Bluetooth setup is unavailable.");
        const bool queued = std::ranges::any_of(commands, [wifi](const auto& command) {
            if (wifi)
                return command.type == Type::WifiScan || command.type == Type::WifiConnect;
            return command.type == Type::BluetoothRefreshPaired || command.type == Type::BluetoothScan || command.type == Type::BluetoothConnect ||
                   command.type == Type::BluetoothForget;
        });
        if (queued || (wifi ? wifiBusy : bluetoothBusy || (bluetoothScanning && (action == "scan" || action == "paired"))))
            return errorResponse(connection, 409, wifi ? "Wi-Fi is busy." : "Bluetooth is busy.");

        if (action == "scan" && body.empty()) {
            commands.push_back({wifi ? Type::WifiScan : Type::BluetoothScan});
        } else if (!wifi && action == "paired" && body.empty()) {
            commands.push_back({Type::BluetoothRefreshPaired});
        } else if (wifi && action == "connect") {
            const auto separator = body.find('\n');
            if (separator == std::string_view::npos || !passwordText(body.substr(separator + 1)))
                return errorResponse(connection, 400, "Invalid Wi-Fi connection request.");
            const auto network = std::ranges::find(wifiNetworks, body.substr(0, separator), &WifiNetwork::id);
            if (network == wifiNetworks.end() || network->connected)
                return errorResponse(connection, 409, "This network is no longer available to join. Scan again.");
            if (network->security == WifiNetwork::Security::Other)
                return errorResponse(connection, 400, "This network uses an unsupported security type.");
            WebConfigCommand command{Type::WifiConnect};
            command.target = network->id;
            command.wifiPassword = body.substr(separator + 1);
            commands.push_back(std::move(command));
        } else if (!wifi && (action == "connect" || action == "forget")) {
            const bool forget = action == "forget";
            const auto device = std::ranges::find(bluetoothDevices, body, &WebConfigState::Bluetooth::Device::id);
            if (device == bluetoothDevices.end() || (forget ? !device->paired : device->connected))
                return errorResponse(connection, 409, "This Bluetooth device is no longer available for that action.");
            WebConfigCommand command{forget ? Type::BluetoothForget : Type::BluetoothConnect};
            command.target = device->id;
            commands.push_back(std::move(command));
        } else {
            return errorResponse(connection, 404, "Network control not found.");
        }
        (wifi ? wifiBusy : bluetoothBusy) = true;
        return respond(connection, 202, "application/json", "{}");
    }

    static void complete(void*, MHD_Connection*, void** request, MHD_RequestTerminationCode)
    {
        delete static_cast<Request*>(*request);
        *request = nullptr;
    }

    static MHD_Result access(void* self, MHD_Connection* connection, const char* url, const char* method,
                             const char*, const char* data, size_t* size, void** request)
    {
        if (!*request) {
            *request = new (std::nothrow) Request;
            return *request ? MHD_YES : MHD_NO;
        }
        auto& context = *static_cast<Request*>(*request);
        try {
            if (*size) {
                const auto limit = url == otaUrlPath ? otaUrlLimit : requestLimit;
                context.tooLarge = context.tooLarge || *size > limit - context.body.size();
                if (!context.tooLarge)
                    context.body.append(data, *size);
                *size = 0;
                return MHD_YES;
            }
            if (context.tooLarge)
                return errorResponse(connection, 413, "Request is too large.");
            return static_cast<Server*>(self)->route(connection, url, method, context.body);
        } catch (...) {
            return MHD_NO;
        }
    }

    MHD_Result route(MHD_Connection* connection, std::string_view path, std::string_view method, std::string_view body)
    {
        const auto requestHost = header(connection, "Host");
        if (!acceptsHost(connection, requestHost, machineHost))
            return errorResponse(connection, 403, "Open Web Config using the DeckDS IP address and port 8080.");
        const char* cookie = MHD_lookup_connection_value(connection, MHD_COOKIE_KIND, sessionCookie);
        const std::string_view token = cookie ? cookie : "";
        const bool authorized = !password.enabled() || password.acceptsSession(token);
        if (method == "GET") {
            if (path == "/" || path == "/debug") {
                if (!authorized)
                    return respond(connection, 303, "text/plain", "", "Location", path == "/debug" ? "/login?next=debug" : "/login");
                const auto page = path == "/" ? indexHtml : debugHtml;
                if (password.enabled())
                    return respond(connection, 200, "text/html; charset=utf-8", page, "Set-Cookie", rememberSession(token));
                return respond(connection, 200, "text/html; charset=utf-8", page);
            }
            if (path == "/login") {
                if (authorized)
                    return respond(connection, 303, "text/plain", "", "Location", "/");
                return respond(connection, 200, "text/html; charset=utf-8", loginHtml);
            }
            if (path == "/bootstrap.min.css") return respond(connection, 200, "text/css; charset=utf-8", bootstrapCss);
            if (path == "/config.css") return respond(connection, 200, "text/css; charset=utf-8", pageStyle);
            if (path == "/config.js") return respond(connection, 200, "text/javascript; charset=utf-8", pageScript);
            if (path == "/debug.js") return respond(connection, 200, "text/javascript; charset=utf-8", debugScript);
            if (path == "/login.js") return respond(connection, 200, "text/javascript; charset=utf-8", loginScript);
            if (path == "/logo.svg") return respond(connection, 200, "image/svg+xml", logoSvg);
        } else if (method == "POST") {
            if (path == "/api/debug")
                return errorResponse(connection, 405, "Debug is read-only.");
            const auto origin = header(connection, "Origin");
            if (!origin.empty() && origin != "http://" + std::string(requestHost))
                return errorResponse(connection, 403, "Send changes from the DeckDS Web Config page.");
            const auto type = header(connection, "Content-Type");
            if (type.substr(0, type.find(';')) != "text/plain")
                return errorResponse(connection, 415, "Expected plain text.");
        } else {
            return errorResponse(connection, 405, "Method is unavailable.");
        }

        if (!path.starts_with("/api/"))
            return errorResponse(connection, 404, "Page not found.");
        if (path == loginPath) {
            if (method == "POST")
                return signIn(connection, body);
            return errorResponse(connection, 405, "Method is unavailable.");
        }
        if (!authorized)
            return errorResponse(connection, 401, "Sign in to Web Config.");
        if (path == passwordPath)
            return accessPassword(connection, method, body);
        const std::lock_guard lock(mutex);
        if (method == "GET") {
            if (path == "/api/state")
                return respond(connection, 200, "application/json", state);
            if (path == "/api/debug") {
                if (debugState.empty())
                    return errorResponse(connection, 503, "Waiting for the first debug snapshot.");
                return respond(connection, 200, "application/json", debugState);
            }
            return errorResponse(connection, 404, "Page not found.");
        }

        using Type = WebConfigCommand::Type;
        if (commands.size() >= commandLimit)
            return errorResponse(connection, 503, "DeckDS is busy. Try again shortly.");
        if (path == "/api/scan") {
            if (body != "quick" && body != "full")
                return errorResponse(connection, 400, "Choose Quick Scan or Full Scan.");
            if (!canScan || std::ranges::any_of(commands, [](const auto& command) {
                    return command.type == Type::QuickScan || command.type == Type::FullScan;
                }))
                return errorResponse(connection, 409, "A scan is unavailable right now.");
            commands.push_back({body == "quick" ? Type::QuickScan : Type::FullScan});
            canScan = false;
        } else if (path == "/api/missing") {
            const auto split = body.find(':');
            const auto review = number<uint64_t>(body.substr(0, split));
            const auto choice = split == std::string_view::npos ? std::string_view{} : body.substr(split + 1);
            if (!review || (choice != "keep" && choice != "remove"))
                return errorResponse(connection, 400, "Choose Keep or Remove.");
            if (!activeReview || *review != activeReview)
                return errorResponse(connection, 409, "That scan has already been reviewed.");
            commands.push_back({choice == "keep" ? Type::KeepMissing : Type::RemoveMissing, *review});
            activeReview = 0;
        } else if (path == otaUrlPath) {
            if (!validOtaUrl(body))
                return errorResponse(connection, 400, "Enter a valid HTTP or HTTPS URL.");
            WebConfigCommand command{Type::OtaUrl};
            command.otaUrl = body;
            commands.push_back(std::move(command));
        } else if (path.starts_with("/api/wifi/") || path.starts_with("/api/bluetooth/")) {
            return networkCommand(connection, path, body);
        } else if (path.starts_with("/api/settings/")) {
            const auto name = path.substr(std::string_view("/api/settings/").size());
            const auto position = std::ranges::find(settingNames, name);
            const size_t index = position - settingNames.begin();
            if (position == settingNames.end())
                return errorResponse(connection, 400, "That setting is unavailable.");
            const auto setting = static_cast<WebSetting>(index);
            int theme = 0;
            if (setting == WebSetting::Accent) {
                const auto separator = body.find(':');
                const auto targetTheme = number<int>(body.substr(0, separator));
                if (separator == std::string_view::npos || !targetTheme || *targetTheme < 0 || *targetTheme > settingMax[0])
                    return errorResponse(connection, 400, "That theme is unavailable.");
                theme = *targetTheme;
                body.remove_prefix(separator + 1);
            }
            const auto value = number<int>(body);
            if (!value || *value < 0 || *value > settingMax[index])
                return errorResponse(connection, 400, "That setting value is unavailable.");
            WebConfigCommand command{Type::Setting, 0, setting, *value};
            command.theme = theme;
            commands.push_back(std::move(command));
        } else if (path.starts_with("/api/playback/")) {
            const auto name = path.substr(std::string_view("/api/playback/").size());
            const auto position = std::ranges::find(playbackNames, name);
            if (position == playbackNames.end())
                return errorResponse(connection, 404, "Playback control not found.");
            using Action = WebPlaybackCommand::Action;
            WebConfigCommand command{Type::Playback};
            command.playback.action = static_cast<Action>(position - playbackNames.begin());
            if (command.playback.action == Action::Seek) {
                const auto split = body.rfind(':');
                const auto offset = split == std::string_view::npos ? std::nullopt : number<int64_t>(body.substr(split + 1));
                if (split == 0 || !offset || *offset < 0)
                    return errorResponse(connection, 400, "Expected the track ID and a position in milliseconds.");
                if (body.substr(0, split) != playbackTrackId)
                    return errorResponse(connection, 409, "The track changed. Seek again on the current track.");
                command.playback.trackId = body.substr(0, split);
                command.playback.value = *offset;
            } else if (command.playback.action == Action::Volume || command.playback.action == Action::Mute) {
                const auto value = number<int>(body);
                const int maximum = command.playback.action == Action::Mute ? 1 : 100;
                if (!value || *value < 0 || *value > maximum)
                    return errorResponse(connection, 400, "That audio setting is out of range.");
                command.playback.value = *value;
            } else if (!body.empty()) {
                return errorResponse(connection, 400, "This playback control takes an empty request.");
            }
            commands.push_back(std::move(command));
        } else {
            return errorResponse(connection, 404, "Page not found.");
        }
        return respond(connection, 202, "application/json", "{}");
    }
};

WebConfig::WebConfig() : server(std::make_unique<Server>()) {}
WebConfig::~WebConfig() { stop(); }
bool WebConfig::supported() { return true; }
bool WebConfig::running() const { return server->daemon != nullptr; }
std::string WebConfig::error() const { return server->failure; }

bool WebConfig::start(const std::filesystem::path& preferences)
{
    stop();
    if (!server->password.load(preferences)) {
        server->failure = "Unable to read the Web Config password settings";
        return false;
    }
    char machine[256]{};
    if (gethostname(machine, sizeof(machine) - 1) == 0 && *machine) {
        server->machineHost = machine;
        std::ranges::transform(server->machineHost, server->machineHost.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        server->machineHost += ':' + std::to_string(port);
    }
    sockaddr_in bindAddress{};
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_port = htons(port);
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    server->daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, port, nullptr, nullptr, Server::access, server.get(),
        MHD_OPTION_SOCK_ADDR, reinterpret_cast<const sockaddr*>(&bindAddress),
        MHD_OPTION_CONNECTION_LIMIT, 16u, MHD_OPTION_PER_IP_CONNECTION_LIMIT, 8u,
        MHD_OPTION_CONNECTION_TIMEOUT, 5u, MHD_OPTION_CONNECTION_MEMORY_LIMIT, size_t{16'384},
        MHD_OPTION_NOTIFY_COMPLETED, Server::complete, nullptr, MHD_OPTION_END);
    if (!server->daemon) {
        stop();
        server->failure = "Unable to listen on port 8080";
        return false;
    }
    return true;
}

void WebConfig::stop()
{
    if (server->daemon)
        MHD_stop_daemon(std::exchange(server->daemon, nullptr));
    server->machineHost.clear();
    server->failure.clear();
    server->commands.clear();
    server->playbackTrackId.clear();
    server->canScan = false;
    server->activeReview = 0;
    server->wifiSupported = server->wifiBusy = false;
    server->bluetoothSupported = server->bluetoothBusy = server->bluetoothScanning = false;
    server->wifiNetworks.clear();
    server->bluetoothDevices.clear();
    server->state = json({});
    server->debugState.clear();
}

void WebConfig::publish(const WebConfigState& state)
{
    auto encoded = json(state);
    const std::lock_guard lock(server->mutex);
    server->state = std::move(encoded);
    server->playbackTrackId = state.playback.trackId;
    server->canScan = state.canScan;
    server->activeReview = state.removing || state.missing == 0 ? 0 : state.review;
    server->wifiSupported = state.wifi.supported;
    server->wifiBusy = state.wifi.busy;
    server->wifiNetworks = state.wifi.networks;
    server->bluetoothSupported = state.bluetooth.supported;
    server->bluetoothBusy = state.bluetooth.busy;
    server->bluetoothScanning = state.bluetooth.scanning;
    server->bluetoothDevices = state.bluetooth.devices;
}

void WebConfig::publishDebug(const WebDebugState& state)
{
    auto encoded = debugJson(state);
    const std::lock_guard lock(server->mutex);
    server->debugState = std::move(encoded);
}

std::vector<WebConfigCommand> WebConfig::takeCommands()
{
    const std::lock_guard lock(server->mutex);
    return std::exchange(server->commands, {});
}
#else
struct WebConfig::Server {};
WebConfig::WebConfig() = default;
WebConfig::~WebConfig() = default;
bool WebConfig::supported() { return false; }
bool WebConfig::running() const { return false; }
bool WebConfig::start(const std::filesystem::path&) { return false; }
void WebConfig::stop() {}
std::string WebConfig::error() const { return {}; }
void WebConfig::publish(const WebConfigState&) {}
void WebConfig::publishDebug(const WebDebugState&) {}
std::vector<WebConfigCommand> WebConfig::takeCommands() { return {}; }
#endif
