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
configuration and retention period still need to be documented.

## WebSocket-to-UDP relay

The relay can observe the browser connection's IP address, target game-server
address, connection times and relayed traffic metadata. It forwards game
traffic to third-party Enemy Territory servers, which independently receive
game-protocol identifiers and player information.

Questions to complete:

- production endpoint(s), reverse proxy and hosting provider;
- access/application log fields and retention;
- abuse prevention data and ban lists;
- whether payloads are ever persisted;
- technical and organisational access controls.

## P2P lobby and fallback relay

The lobby is hosted at `etclan.de` (`135.125.189.21`); its public scheme, port
and path still need confirmation. It processes room metadata and WebSocket
connection information. WebRTC negotiation may reveal network addresses to the
participating peers and configured STUN/TURN operators. When direct WebRTC is
unavailable, game packets may pass through the lobby fallback relay.

Questions to complete:

- exact public lobby URL;
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

