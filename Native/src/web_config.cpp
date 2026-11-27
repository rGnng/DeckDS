#include "web_config.hpp"

#if defined(DECKDS_WEB_CONFIG)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <microhttpd.h>
#include <net/if.h>
#include <unistd.h>

#include <algorithm>
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
constexpr std::array<std::string_view, 5> settingNames{"theme", "brightness", "clock", "sfx", "lid"};
constexpr std::array<int, 5> settingMax{2, 6, 1, 1, 1};

std::string lanAddress()
{
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0)
        return {};
    const std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> addresses(interfaces, freeifaddrs);
    for (auto* entry = interfaces; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET || !(entry->ifa_flags & IFF_UP) ||
            (entry->ifa_flags & (IFF_LOOPBACK | IFF_POINTOPOINT)))
            continue;
        const auto& address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr)->sin_addr;
        const auto ip = ntohl(address.s_addr);
        if ((ip >> 24) != 10 && (ip >> 20) != 0xac1 && (ip >> 16) != 0xc0a8)
            continue;
        char text[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &address, text, sizeof(text)))
            return text;
    }
    return {};
}

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
    output << "}}";
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

MHD_Result respond(MHD_Connection* connection, unsigned status, std::string_view type, std::string_view body)
{
    auto* response = MHD_create_response_from_buffer(body.size(), const_cast<char*>(body.data()), MHD_RESPMEM_MUST_COPY);
    if (!response)
        return MHD_NO;
    MHD_add_response_header(response, "Content-Type", type.data());
    MHD_add_response_header(response, "Cache-Control", "no-store");
    MHD_add_response_header(response, "X-Content-Type-Options", "nosniff");
    MHD_add_response_header(response, "Referrer-Policy", "no-referrer");
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
    bool canScan = false;
    uint64_t activeReview = 0;

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
                context.tooLarge = context.tooLarge || *size > requestLimit - context.body.size();
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
            return errorResponse(connection, 403, "Open the address shown on DeckDS.");
        if (method == "GET") {
            if (path == "/") return respond(connection, 200, "text/html; charset=utf-8", indexHtml);
            if (path == "/debug") return respond(connection, 200, "text/html; charset=utf-8", debugHtml);
            if (path == "/bootstrap.min.css") return respond(connection, 200, "text/css; charset=utf-8", bootstrapCss);
            if (path == "/config.css") return respond(connection, 200, "text/css; charset=utf-8", pageStyle);
            if (path == "/config.js") return respond(connection, 200, "text/javascript; charset=utf-8", pageScript);
            if (path == "/debug.js") return respond(connection, 200, "text/javascript; charset=utf-8", debugScript);
            if (path == "/logo.svg") return respond(connection, 200, "image/svg+xml", logoSvg);
        } else if (method == "POST") {
            if (path == "/api/debug")
                return errorResponse(connection, 405, "Debug is read-only.");
            const auto origin = header(connection, "Origin");
            if (!origin.empty() && origin != "http://" + std::string(requestHost))
                return errorResponse(connection, 403, "Open the address shown on DeckDS.");
            const auto type = header(connection, "Content-Type");
            if (type.substr(0, type.find(';')) != "text/plain")
                return errorResponse(connection, 415, "Expected plain text.");
        } else {
            return errorResponse(connection, 405, "Method is unavailable.");
        }

        if (!path.starts_with("/api/"))
            return errorResponse(connection, 404, "Page not found.");
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
        } else if (path.starts_with("/api/settings/")) {
            const auto name = path.substr(std::string_view("/api/settings/").size());
            const auto position = std::ranges::find(settingNames, name);
            const auto value = number<int>(body);
            const size_t index = position - settingNames.begin();
            if (position == settingNames.end() || !value || *value < 0 || *value > settingMax[index])
                return errorResponse(connection, 400, "That setting value is unavailable.");
            commands.push_back({Type::Setting, 0, static_cast<WebSetting>(index), *value});
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
std::string WebConfig::address() const
{
    if (!running())
        return {};
    auto ip = lanAddress();
    if (ip.empty())
        ip = "127.0.0.1";
    return "http://" + ip + ':' + std::to_string(port);
}
std::string WebConfig::error() const { return server->failure; }

bool WebConfig::start()
{
    stop();
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
    server->canScan = false;
    server->activeReview = 0;
    server->state = json({});
    server->debugState.clear();
}

void WebConfig::publish(const WebConfigState& state)
{
    auto encoded = json(state);
    const std::lock_guard lock(server->mutex);
    server->state = std::move(encoded);
    server->canScan = state.canScan;
    server->activeReview = state.removing || state.missing == 0 ? 0 : state.review;
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
bool WebConfig::start() { return false; }
void WebConfig::stop() {}
std::string WebConfig::address() const { return {}; }
std::string WebConfig::error() const { return {}; }
void WebConfig::publish(const WebConfigState&) {}
void WebConfig::publishDebug(const WebDebugState&) {}
std::vector<WebConfigCommand> WebConfig::takeCommands() { return {}; }
#endif
