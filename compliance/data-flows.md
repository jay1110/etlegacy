# Preliminary deployment data-flow register

This is a technical inventory for the privacy review. It deliberately contains
no private postal address or other non-public operator details.

## Browser application

The application stores game files, downloaded PK3 files, WASM modules, player
profiles and host settings in IndexedDB. It also uses browser storage for such
items as favourites, the ET key/GUID and short-lived host/lobby restoration
state. These values normally remain on the user's device, but player identity
and game protocol fields can be sent to joined game servers.

Questions to complete:

- exact keys and retention behaviour for every localStorage/sessionStorage item;
- UI mechanism to inspect and erase all locally stored game data;
- whether any identifier is reused across unrelated servers;
- wording and legal basis for strictly necessary browser storage.

## Web hosting and downloads

The three known web deployments deliver the application and game data. Normal
HTTP operation exposes at least the requesting IP address, timestamp, requested
path and user-agent to the web host or reverse proxy. The actual access-log
configuration confirms separate HTTP and HTTPS proxy access/error logs for both
`et.clan-etc.de` and `etclan.de` below `/var/www/vhosts/system/<domain>/logs/`.
The WebSocket locations do not disable access logging and therefore inherit
the HTTPS virtual host's access log. Ten compressed `processed` generations
were observed for both HTTP and HTTPS access logs. Their dates span multiple
months, so this is not a simple daily retention window. Proxy error-log
archives reach back into 2025, and other error-log archives were observed from
2023. `/etc/logrotate.d/nginx` only covers `/var/log/nginx/*.log`; Plesk manages
these per-vhost logs separately. Plesk is configured to rotate a log after
10,240 KB, compress it and retain ten rotated files. Because that policy has no
calendar limit, low-traffic logs can remain for a very long time. A
proportionate time-based maximum still needs to be selected.

## WebSocket-to-UDP relay

The public relay endpoint is
`wss://et.clan-etc.de/ws-relay/<host>:<port>`. Nginx terminates the public
connection and proxies it to `ws://127.0.0.1:8080/<host>:<port>`. The reverse
proxy can observe the browser connection's IP address, target game-server
address, connection times and relayed traffic metadata. With the currently
documented proxy headers the Node relay itself receives Nginx as its direct
peer; no `X-Forwarded-For` or `X-Real-IP` header is configured in this location.
It forwards game traffic to third-party Enemy Territory servers, which
independently receive game-protocol identifiers and player information.

Questions to complete:

- hosting provider and complete Nginx access-log configuration;
- access/application log fields and retention;
- abuse prevention data and ban lists;
- whether payloads are ever persisted;
- technical and organisational access controls.

## P2P lobby and fallback relay

The lobby is publicly available at
`wss://et.clan-etc.de/p2p-lobby/` (`135.125.189.21`). Nginx proxies it to
`ws://127.0.0.1:8081/`. It processes room metadata and WebSocket connection
information. With the currently documented proxy headers the lobby sees Nginx
as its direct WebSocket peer, while the public client address can remain in the
Nginx access log. WebRTC negotiation may reveal network addresses to the
participating peers and configured STUN/TURN operators. When direct WebRTC is
unavailable, game packets may pass through the lobby fallback relay.

The repository example service starts the lobby without `--ice`, `--turn-user`
or `--turn-pass`. Production runs both the lobby and WebSocket relay under PM2,
not the repository's example systemd unit. Unless the PM2 arguments or
environment
overrides that configuration, the lobby therefore advertises Google's public
STUN endpoint `stun:stun.l.google.com:19302`, uses no TURN server, and relies on
the built-in WebSocket fallback relay when a direct WebRTC path cannot be made.
The production PM2 configuration still needs to be checked because it can
override all three values outside this repository.

Questions to complete:

- effective Plesk rotation thresholds and maximum retention;
- whether production overrides `ETL_LOBBY_ICE` or the service command line;
- whether TURN credentials are configured (record only that they exist, never the secret);
- STUN and TURN operators and their privacy terms;
- room metadata exposed publicly;
- signalling and fallback-relay logging;
- host-token lifetime and protection;
- cleanup and retention after a room closes.

## Public server list

The application requests the public server list from
`https://etclan.de/serverlist.php`. The service receives ordinary HTTP request
metadata. Server addresses, names, player counts and possibly player names are
obtained from publicly reachable game servers and displayed to users.

Questions to complete:

- whether player names are returned or stored;
- scan frequency and retention;
- opt-out and abuse contact for server operators;
- proxy/access-log retention.

## External download sources

Map, mod and game-data downloads can contact hosts named in the map list or in
deployment configuration. Those hosts receive the user's IP address and HTTP
metadata unless the download is routed through the deployment's same-origin
proxy. The final privacy notice must identify the categories of recipients and
explain when a direct third-party request occurs.
