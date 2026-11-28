# Library namespaces, UUIDs, and ReKey

A library has a permanent library UUID, an editable identifier, and a stored current namespace. The namespace participates in every song UUID for that library. Tracks and WebTracks initialize it when they create the library row. DeckDS initializes it on Add and when upgrading an older index. An identifier edit saves the input for the next ReKey. ReKey activates it by writing the namespace and updating the song IDs that depend on it. [Tracks library creation and identifier updates](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/LibraryDatabase.swift#L874-L945) [WebTracks library creation and identifier updates](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/tracks_db.py#L251-L363) [DeckDS startup migration](../Native/src/media.cpp#L560)

## What each field means

| Field | Meaning | When it changes |
| --- | --- | --- |
| Library UUID | Persistent ID of the library database row. DeckDS also uses it as the owner of indexed tracks. | Generated from the canonical root path at creation. Relocate keeps the same row and UUID. |
| Library identifier | Editable name used as the input for the next namespace. DeckDS retains its display casing. | Set during Add and editable afterward. An omitted identifier on Add generates a name such as `library1`. |
| Current namespace | Stored normalized identifier used to compute the library’s current song UUIDs. | Initialized during normal library creation. ReKey updates it from the saved identifier. |
| Root | Current absolute location of the library’s files. | Set on Add and updated on confirmed Relocate. |
| Original root | DeckDS’s record of the initial root. It also locates the old per-folder playback settings on upgrade. | Set on creation and retained after relocation. |
| Sync URL | Optional per-library connection setting. | Set or cleared in Add or Edit. |

The Library row shows the editable identifier and the stored namespace separately. A dash denotes an empty stored namespace. The browser reads the value from SQLite through `libraryConfig.folders[].namespace` in `GET /api/state`. [DeckDS Web Config serialization](../Native/src/web_config.cpp#L211) [DeckDS library row](../Native/web/config.js#L479) [Tracks settings row](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/AudiaApp.swift#L965-L988) [WebTracks library row](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/static/js/app.js#L4476-L4486)

## Exact UUID inputs

The identity functions build a string, encode it as UTF-8, and take the 16 bytes of its MD5 digest. They print those bytes as uppercase hex with UUID hyphen grouping of 8, 4, 4, 4, and 12 digits. The digest bytes are used directly, including the bits that usually represent UUID version and variant. The vertical bars in the table are literal ASCII separators. [Tracks identity functions](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/SongIdentity.swift#L3-L54) [Tracks digest formatting](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/FLACLibrary.swift#L630-L636) [WebTracks identity functions](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/tracks_identity.py) [DeckDS identity functions](../Native/src/music_identity.cpp#L118)

| UUID | String passed to MD5 |
| --- | --- |
| Library | `library` + `\|` + the canonical absolute root path at creation |
| Song | `song` + `\|` + normalized current namespace + `\|` + normalized path relative to the root |
| Artist | `artist` + `\|` + normalized artist name |
| Album | `album` + `\|` + the uppercase UUID of the **album artist** + `\|` + normalized album title |

### Normalization

- **Identifier and namespace.** Trim surrounding whitespace and newlines, normalize to Unicode NFC, and lowercase for the namespace. DeckDS applies the trim and NFC steps to the identifier it stores for display. Internal spaces remain significant.
- **Song path.** Resolve ownership against the library root and use the relative path with forward slashes. Apply Unicode NFC and strip leading `/` characters. Case, spaces, subdirectory names, and the filename extension are significant. DeckDS stores the relative path during indexing and uses that stored path as input at load and ReKey.
- **Artist names and album titles.** Apply NFC and locale-independent lowercase. The identity helper keeps surrounding spaces. A scanner may trim a tag before passing it to the helper. DeckDS keeps a separate set of identity tags to account for tag extraction and display fallbacks.
- **Library root.** The library UUID helper hashes the canonical root string supplied by its caller. The Add flow resolves the root to an absolute existing directory before computing it. The helper leaves path casing as supplied. The old DeckDS import uses the root already stored by the old application.

DeckDS uses Core Foundation on macOS and ICU on Linux. Both compensate for Greek final sigma so `ΟΣ` lowercases to `οσ`, as in Swift and WebTracks’s character-by-character lowercase. Whitespace definitions can differ at unusual boundaries. Foundation and DeckDS trim U+200B at the ends of a namespace. WebTracks uses Python’s `strip()`, which retains that character. Invalid UTF-8 in identity input raises an error in DeckDS. [Tracks normalization](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/SongIdentity.swift#L10-L48) [WebTracks normalization](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/tracks_identity.py#L23-L68) [DeckDS normalization](../Native/src/music_identity.cpp#L23)

### Worked example

Assume a library was created at `/Music/CDs` with the identifier `iTunes`. The song occupies `Disc 1/Café.flac` within that library. Editing the identifier to `iTunes12` leaves the current namespace at `itunes`. A successful ReKey writes `itunes12` and changes the song UUID from the first song row to the second.

| Input | Result |
| --- | --- |
| Library root `/Music/CDs` | `242D9A86-4AB5-40D8-C2C9-F56BDB588EF7` |
| Namespace `itunes`, path `Disc 1/Café.flac` | `6BE93987-E700-C1A4-B700-410A2D6456AE` |
| Namespace `itunes12`, same relative path | `051DC1D1-DD36-CD18-A6A9-360FF9C24FED` |
| Namespace `extras`, same relative path | `A0CFC44B-95B9-09CE-C2DE-A999F8610BC9` |
| Namespace `itunes12`, path `Disc 2/Café.flac` | `52CAE37F-0E29-57FD-52AA-FC248A3B033B` |
| Track artist `Björk` | `791CE1EC-6E38-D8E9-3D3E-B17DB809F7B7` |
| Album artist `ÄRTIST` | `10630398-87EA-5525-4847-BF98CF765B01` |
| Album artist `ÄRTIST`, title `Café Live` | `45649E53-AA44-E2EB-A031-FE96E5700060` |

Composed `é` and decomposed `e` plus U+0301 produce the same song UUID after NFC. The old DeckDS key hashes the raw bytes, so the two spellings in this example produce `be27afae4ed1356e` and `22a7d7313544222e`, respectively. [DeckDS legacy key calculation](../Native/src/media.cpp#L31)

### What affects IDs across devices

- Matching stored namespaces and matching normalized relative paths yield matching song UUIDs across DeckDS, Tracks, and WebTracks, even with different absolute roots. Copying files alone leaves each app’s existing namespace in effect.
- The library UUID may differ across machines when their original canonical roots differ. That UUID is used for library ownership. The song UUID uses the current namespace and relative path.
- Renaming or moving an individual file within a library changes the relative-path input. The same audio at the new path gets the UUID for that new path. Editing the filename-independent track title, genre, year, artwork, duration, or file contents leaves the song UUID tied to the same path.
- An album’s UUID follows the album artist and album title. A track’s artist UUID follows its track artist tag. A compilation can have many track artist IDs and a single album artist and album ID. Equal normalized artist names and equal album-artist/title pairs can share IDs across libraries.
- WebTracks supports standard UUID text, compact hex, and URL-safe Base64 for the same 16 UUID bytes. Those are link encodings of the ID. Matching an album link also requires matching identity tags. [Tracks and WebTracks link compatibility](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/README.md#L135-L180)

## Creation, editing, and relocation

**Tracks** validates the directory and identifier, derives the namespace, and inserts the identifier and namespace together. Its path-conflict update preserves both identity fields on an existing row. **WebTracks** also derives and writes both fields when it creates a row. Its compatibility importer takes old configuration paths, assigns each new path a generated identifier, derives a namespace immediately, and keeps an existing row when its path is already indexed. **DeckDS Add** also writes both fields and reserves the namespace in the same transaction. [Tracks Add flow](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/Library.swift#L365-L423) [Tracks database insert](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/LibraryDatabase.swift#L874-L899) [WebTracks legacy path import](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/tracks_db.py#L545-L577) [DeckDS Add flow](../Native/src/media.cpp#L686)

**Edit** saves the identifier, and in DeckDS it can also save the Sync URL. The current namespace stays frozen. Add and Edit validate identifier uniqueness against other libraries. DeckDS compares identifiers after normalization, rejects a normalized empty identifier on Edit and identifiers over 128 bytes, and reserves every namespace previously used by a library. Historical reservations keep an old song UUID from resolving to a different library if the same namespace and relative path are reused. The user’s old bookmarks in playback settings can still resolve through aliases. [Tracks identity validation](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/SongIdentity.swift#L58-L100) [WebTracks identifier edit](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/tracks_db.py#L309-L363) [DeckDS validation and namespace reservation](../Native/src/media.cpp#L458)

**Relocate** updates the root of an existing row. DeckDS previews matches using the complete relative path, including subdirectories. It checks that each matched path is a regular file within the new root. Confirmed Relocate rechecks the plan, updates the root and any changes made in Edit in the same transaction, and marks unmatched rows as missing for review. The library UUID, original root, current namespace, and song IDs are retained. A matching filename in another subdirectory is a separate path. [Tracks relocation](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/LibraryDatabase.swift#L978) [WebTracks relocation](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/tracks_db.py#L365-L438) [DeckDS relocation](../Native/src/media.cpp#L974)

## How the data is stored

Tracks and WebTracks persist UUIDs in the `songs`, `albums`, `artists`, and `libraries` tables. They use song IDs to join playlist items to songs. DeckDS stores indexed file rows by library and relative path. It derives UUIDs from the stored namespace and identity fields when it loads those rows. Its playback settings are stored separately from SQLite. The same UUID algorithm can yield interoperable IDs across the three apps while each app manages its own database. [Tracks database schema](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/LibraryDatabase.swift#L241-L313) [WebTracks database schema](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/tracks_db.py#L25-L102) [DeckDS schema and record loading](../Native/src/media.cpp#L294)

| DeckDS record | Stored values relevant to identity |
| --- | --- |
| `music_sources` | `id`, unique `root`, `original_root`, `identifier`, `id_namespace`, nullable `sync_url`, and `enabled`. |
| `music_entries` | Primary key `(source, relative)`, indexed metadata, file size and modification stamp, missing flag, and separate artist, album, and album-artist identity inputs. |
| `music_namespaces` | Namespace as primary key with the owner’s library ID. Previously used namespaces remain reserved. |
| `music_id_aliases` | Old song ID as primary key with the owning library ID and relative path. Resolution uses the current namespace of that file’s source. |
| `preview-libraries.conf` | Current song and resume position, ordered queue, history, played list, stars, playlist names and members, plus interface and audio settings. Songs are referenced by ID. |
| `preview-music-<root-key>.conf` | Previous per-folder settings file. DeckDS tries the copy keyed by the first library’s original root when the combined settings file is absent. |

On macOS, these files live in the DeckDS application support directory. The native app finds the platform preference directory through SDL. `library.sqlite` stores library data. The current song, queue, and other playback fields are written in the `deckds-preview-1` format. [DeckDS preference directory](../Native/src/main.cpp#L296) [DeckDS settings selection](../Native/src/main.cpp#L510) [DeckDS playback settings format](../Native/src/preview_state.cpp#L93)

### Where album and artist identity tags come from

DeckDS reads tags with FFmpeg, preferring a nonempty tag on the selected audio stream before looking at the container. It stores identity inputs separately from the display fields. For example, the displayed album can fall back to a folder name, while the identity album can be `Unknown Album`. For a FLAC file with an album tag, the identity title keeps the tag’s spaces. For other files it trims the title before using it. The track artist can fall back to the album artist, and the album artist can fall back to the track artist. The tag reader recognizes album-artist variants and the `ensemble` and `band` fallbacks. For FLAC, an absent track artist uses the primary album-artist tags or `Unknown Artist`. When identity inputs are unavailable during catalog load, DeckDS uses the indexed display values. Tracks uses its own FLAC and AVFoundation readers. WebTracks ReKey consumes its already indexed names and album rows. Matching normalized tags is the condition for matching album and artist UUIDs across those readers. [DeckDS FFmpeg tag extraction](../Native/src/media.cpp#L145) [DeckDS derived album and artist IDs](../Native/src/media.cpp#L391) [Tracks tag readers](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/FLACLibrary.swift#L96-L124) [WebTracks indexed artist and album inputs](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/tracks_db.py#L1125-L1188)

### Songs without an owner in the reference databases

During normal indexing, Tracks and WebTracks have a fallback that hashes the full file path directly if the song has no usable library owner and relative path. During ReKey, each preserves the previously stored UUID of an unresolved song. DeckDS’s indexed songs belong to a source and have a stored path relative to that source. Its older FNV key is a separate compatibility case used for a source with an empty namespace. [Tracks song insert](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/LibraryDatabase.swift#L1220-L1237) [WebTracks song insert](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/tracks_db.py#L731-L765) [DeckDS song identity selection](../Native/src/media.cpp#L391)

## ReKey and its writes

### Tracks

Confirmation stops playback and opens a progress window. The library refreshes enabled roots from their current files before entering the database ReKey. The database computes each library’s new namespace from its saved identifier and validates the set. It looks up stored song ownership, falling back to the most specific containing library. A song without a usable owner and relative path keeps its old song ID. It computes album and artist UUIDs from the indexed names, merges album metadata where IDs converge, and rejects duplicate song IDs.

The database backs up its current snapshot before the ID rewrite. In one SQLite transaction it rebuilds songs, albums, artists, and playlist item references, remaps saved queue IDs, and writes the new library namespaces. The song rewrite carries over metadata such as dates, play counts, ratings, and stars. The app also remaps persisted history and album artwork and restarts after completion so its in-memory IDs reload. [Tracks confirmation and restart](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/AudiaApp.swift#L289-L297) [Tracks scan and post-processing](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/Library.swift#L470-L531) [Tracks database ReKey](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Sources/Audia/LibraryDatabase.swift#L460-L826)

### WebTracks

Confirmation starts a single background job. The job reads the saved identifiers and existing indexed file and tag data. It uses stored ownership or finds the containing library and derives a relative path. It retains an unresolved song’s ID. It computes new song, album, and artist UUIDs and checks for duplicate song IDs. It creates a SQLite backup alongside the database and writes the replacement song, album, artist, playlist-item and namespace records in a transaction. The job remaps cached artwork by album ID and reloads the server’s in-memory catalog. The UI polls the job status and offers to reload the player after completion. [WebTracks confirmation flow](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/static/js/app.js#L5665-L5715) [WebTracks job](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/server.py#L613-L713) [WebTracks database ReKey](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/tracks_db.py#L1066-L1344)

### DeckDS

1. **Confirm.** The modal sends one `POST /api/library/manage/rekey` after confirmation. HTTP 202 means the command is queued. The app excludes other library operations while ReKey runs. The Library header reads the number of indexed files processed and the total from the published job state. The operation covers all configured libraries.
2. **Back up.** The app pauses playback and writes a playback snapshot as `playback.conf` under `Library Backups/<operation directory>/`. SQLite’s backup API writes the database snapshot to `library.sqlite` in the same directory.
3. **Prepare the namespace and song mapping.** The worker opens a write transaction with `BEGIN IMMEDIATE` in WAL mode. It checks each normalized identifier, calculates every indexed row’s new song UUID, and rejects duplicates. Each changed old ID is associated with its source and relative path. An already matching song ID needs no new alias.
4. **Recover identity tags as needed.** The worker uses cached identity tags when present. For older entries without them, it reads metadata once when the underlying file is available and caches the identity inputs. An unavailable file keeps its indexed metadata and remains eligible for a song ID because the indexed relative path supplies that input.
5. **Commit.** In one SQLite transaction the worker persists old-ID aliases, recovered tags, namespace reservations, and the new `music_sources.id_namespace` values. Cancellation, invalid data, and collisions before commit roll back that transaction. Audio files and their tags are read-only during this process.
6. **Reload and remap.** DeckDS loads the updated catalog and derives song, album, and artist UUIDs from the committed records. It resolves saved track references against current IDs and historical aliases. It restores position, queue, history, played entries, stars, and playlist order, then saves `preview-libraries.conf` with the current IDs by replacing it with a temporary file. SQLite and the settings file are separate writes. A restart between them can still load old song references through the aliases already committed in SQLite.

An alias stores the original song’s source and path. Its current target is calculated on load using the source’s latest namespace. This means an old ID can go straight to the most recent ID after multiple ReKeys. The loader resolves current IDs first and applies aliases for older IDs. Entries whose target has been removed from the index have no live track to resolve. The playback file uses whole seconds for the saved resume position. [DeckDS Web Config command](../Native/src/web_config.cpp#L480) [DeckDS ReKey orchestration and backup](../Native/src/main.cpp#L636) [DeckDS ReKey transaction](../Native/src/media.cpp#L1056) [DeckDS alias resolution](../Native/src/media.cpp#L671) [DeckDS playback remap](../Native/src/main.cpp#L1091) [DeckDS playback settings reader and writer](../Native/src/preview_state.cpp#L21)

For a fully indexed library, the namespace calculation is a short normalization operation and the database write changes one namespace field per library. The per-track work is calculating IDs, checking duplicates, recording changed IDs, and reloading the catalog. The extra time on an older library comes from opening files with uncached identity tags. A second ReKey with already cached tags and unchanged identifiers is typically faster and produces the same IDs. [DeckDS identity loop](../Native/src/media.cpp#L1083)

## Upgrading the old DeckDS index

The earlier DeckDS single-folder index used `music_folder` and `music_files`. It used a 64-bit FNV-1a hash over the raw relative path as the track ID, formatted as 16 lowercase hex characters. The first version of the multi-library import left `id_namespace` empty so the loader would keep using those old keys. The corrected import keeps the old tables as archived index data and reserves a namespace before exposing the library. It preserves old playback references by storing their FNV IDs as aliases. [DeckDS old track ID](../Native/src/media.cpp#L31) [DeckDS startup migration](../Native/src/media.cpp#L560) [DeckDS index import](../Native/src/media.cpp#L614)

1. A fresh import copies the old rows into `music_entries` and assigns the library the identifier `library1`. It commits the namespace `library1` and the old FNV aliases in the same transaction as the import.
2. Startup also repairs an already imported row with an empty namespace. A source with identifier `itunes12` and an empty namespace gets `itunes12`. Startup preserves every populated namespace, including one with a pending identifier edit. Once the migration commits, subsequent startups skip it.
3. Migration checks candidate song IDs across the catalog and rejects a conflicting alias. It preserves the library UUID, roots, membership, sync settings, and missing flags. Its inputs come from the SQLite index. The regular startup scan fills missing artist and album identity tags as it encounters readable files.
4. Startup loads the updated catalog and alias map before reading playback settings. Old playlist members, stars, queue entries, history, and the resume position resolve to UUIDs. DeckDS then saves the restored playback state with the current IDs. If the settings write fails, the committed aliases still resolve the old references on the next startup.
5. Subsequent identifier edits use the normal Edit and ReKey flow. [DeckDS startup migration](../Native/src/media.cpp#L560) [DeckDS playback settings startup](../Native/src/main.cpp#L575)

## Failures and verification

- **Same file in multiple libraries.** Different namespaces give different IDs for equal relative paths. DeckDS rejects overlapping canonical roots and identifiers that normalize to an identifier or namespace reserved by another library.
- **Same normalized path within one namespace.** Two distinct files whose paths collapse under NFC produce the same UUID input. A collision can also arise from literal separators in the inputs because the shared formulas concatenate the fields without escaping. DeckDS checks generated IDs during indexing and ReKey and reports collisions.
- **Missing or offline file.** The indexed relative path still supplies the song UUID. Album and artist IDs come from available metadata. Relocate can keep an unmatched entry for later review. Removing a missing entry removes aliases to that entry.
- **Failed or interrupted ReKey.** A backup may already exist. Failure before SQLite commit leaves the old namespace and aliases in effect. Failure after SQLite commit can leave an old playback settings file. Durable aliases let the loader resolve it against the committed catalog.
- **Identifier changed but UUID unchanged.** Read both database columns. A changed identifier paired with the same current namespace is a pending identifier edit. Compare song IDs using the current namespace in that row. After ReKey compare against the newly stored value.
- **Track UUID differs across apps.** Compare the stored current namespaces and the entire relative path with its case and extension. For album or artist UUIDs, compare the effective identity tags and whether the album uses the album artist’s ID. For UUID links, first decode both representations to the same 16 bytes.

The following SQL statements are read-only checks for an open DeckDS index.

```sql
SELECT id, identifier, quote(id_namespace) AS stored_namespace, root, original_root
FROM music_sources
ORDER BY rowid;

SELECT s.identifier, count(e.relative) AS indexed_files
FROM music_sources AS s
LEFT JOIN music_entries AS e ON e.source = s.id
GROUP BY s.id
ORDER BY s.rowid;

SELECT s.identifier, count(a.old_id) AS old_song_id_aliases
FROM music_sources AS s
LEFT JOIN music_id_aliases AS a ON a.source = s.id
GROUP BY s.id
ORDER BY s.rowid;

PRAGMA integrity_check;
```

A migration test should cover a fresh Add, a pre-namespace database upgrade, an existing empty namespace, a library whose identifier has been edited while its current namespace remains valid, a later manual ReKey, restart with old playback settings, duplicate normalized paths, cancellation, and a relocation that keeps the library and song UUIDs. For cross-app compatibility, run the example keys through both reference implementations and compare the UUIDs byte for byte. [Tracks identity and workflow tests](https://github.com/rGnng/tracks/blob/37fb805bd99686bea424607671a2d0bbbb35a04a/Tests/AudiaTests/LibraryWorkflowTests.swift#L496) [WebTracks compatibility tests](https://github.com/rGnng/webtracks/blob/64147628b580a3abe80b33b07390d7600160e1a2/tests/test_compat.py#L54)
