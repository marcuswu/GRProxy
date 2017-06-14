# GRProxy
Proxy for Ghost Recon with a few hacks mixed in.

I wrote this while I was in college -- somewhere around 2001 or 2002 so the code is crap, but it was a fun little exercize to reverse engineer the network protocol and write this little tool.The code doesn't account for things like typing commands wrong. It'll crash. If I remember correctly, determining what players are on what team was also problematic, so in team games, you may end up shooting the wrong players. Also, teleporting seems to sometimes break replays recorded during a game.

I will probably never update this code to improve it, but if someone sends an MR I'll take a look at it.

## Compile
Compile the proxy with:
```
gcc newgrproxy.c -o newgrproxy
```

## Running

```
newgrproxy server:port proxyport
	hostname:port specifies where to connect
	proxyport specifies a port to listen on (where to connect your game)
```

## Commands
The proxy intercepts chat commands beginning with '/' and tries to interpret them into commands. Any command beginning with a slash will not be sent to the server, so no other player will see them -- even if they are mistyped.

```
/autoaim
```
Turn on automatic aiming. This does not change your view angle. It only redirects the bullet to the nearest opponent. Send again to toggle it off.

```
/autonade
```
Turn on automatic grenades. Grenades no longer travel. They just explode over an enemy. A fast-moving player will not be hit. Send again to toggle it off.

```
/spoof clientid message
```
Spoof a chat message from a given client identifier (see the whois command to find a client id).

```
/showall
```
Show all players on the map. Send again to toggle it off.

```
/fog
```
Turn off fog (perfect view distance). This one sends to your client rather than to the server (it changes only for you).

```
/tele_dist x|y|z distance
```
Teleport a distance along an axis. If you can get inside a wall, wall collision detection on the server breaks, and it's possible to hit anyone anywhere.

```
/tele x|y|z distance clientid
```
Teleport someone else (identified by client id -- see whois command) a distance along an axis.

```
/suicide clientid
```
Tell a player to shot himself. See whois command to find client ids.

```
/suicide_off
```
Turn off suiciding.

```
/whois clientid
```
Determine what player represents a given client id. To find a player, cycle through a sequence of numbers to find the player you're looking for.

```
/tk clientid
```
Tell a player to kill a teammate.
