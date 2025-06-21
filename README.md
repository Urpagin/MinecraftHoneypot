# Minecraft Honeypot

A simple Minecraft honeypot (IP logger) written in C.

# How to Build & Run

1. Clone the repo (or download `main.c`).
2. `gcc -O2 -Wall -Wextra -o honeypot main.c`
3. `./honeypot`

# Compatibility

## Architecture

* **Linux:** Working
* **macOS:** Not tested (but should work)
* **Windows:** Not working (uses `sys/socket.h`, a POSIX-specific header not available on Windows)

## Minecraft Clients

This implementation is compatible with Minecraft Java Edition versions 1.7 and newer, as per the [Minecraft protocol wiki](https://minecraft.wiki/w/Java_Edition_protocol/Server_List_Ping).

You can change the server version by modifying the JSON in the source code (specifically the `name` and `protocol` fields).

# How Does It Work?

First, the program mimics the Minecraft [Server List Ping (SLP)](https://minecraft.wiki/w/Java_Edition_protocol/Server_List_Ping) protocol, implementing only the part that accepts a TCP connection and sends back the bare minimum.

By accepting a connection, the client reveals its IP address. The program then logs this address to a file.

For more details, please read the blog post in the ["Why" section](#why).

# Illustrations

![minecraft multiplayer servers tab](https://github.com/user-attachments/assets/38b2988a-daf1-42ea-aaf6-c2e0b379d481)

![ip logs](https://github.com/Urpagin/MinecraftHoneypot/assets/72459611/0a5a6993-2d1d-4c07-85cf-4964f43631ed)

*(Note: I only used the honeypot locally, which is why all the addresses are 127.0.0.1.)*

# Code Quality Disclaimer

I have never properly learnt C, nor have I created any notable projects in that language prior to this one. That said, the code works, but I am not confident in its quality.

# Why

Written for: [https://blog.urpagin.net/coding-a-minecraft-honeypot/](https://blog.urpagin.net/coding-a-minecraft-honeypot/)

I wrote this blog post because experimenting with sockets and Minecraft is genuinely enjoyable!

# Releases Build Command

Here is the command I use to build the releases (>=1.12):

`gcc -O2 -s -Wall -Wextra -D_FORTIFY_SOURCE=2 -fstack-protector-strong -o honeypot_x86_64_linux_v<version> main.c`
