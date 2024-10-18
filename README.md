# Minecraft Honeypot

Simple Minecraft honeypot (IP logger) written in C.

# How to build

1. Clone the repo (or download `main.c`)

2. `gcc main.c -o honeypot`

3. `./honeypot`


# Compatibility

## Architecture

- Linux: Working
- macOS: Not Tested (but should work)
- Windows: Not Tested (but should work)

## Minecraft clients

According to the [Minecraft protocol wiki](https://wiki.vg/Server_List_Ping), this implementation is compatible from and including Java Edition 1.7 to the latest versions.

This implementation is compatible with Minecraft Java Edition versions 1.7 and newer, as per the [Minecraft protocol wiki](https://wiki.vg/Server_List_Ping).

# How does it work?

First, the program mimics the Minecraft [Server List Ping (SLP)](https://wiki.vg/Server_List_Ping) protocol, to only implement the part that accepts a TCP connection and sends back the bare minimum.

But then, by accepting a connection the client gives out its IP address, after that we just log it into a file.

For more detail, please read the blog post in the ["Why" section](#why).

# Illustrations

![image](https://github.com/Urpagin/MinecraftHoneypot/assets/72459611/54924378-507e-4e4a-b1fa-f68598c2c0f2)

![image](https://github.com/Urpagin/MinecraftHoneypot/assets/72459611/0a5a6993-2d1d-4c07-85cf-4964f43631ed)

# Code Quality Disclaimer

I have never properly learnt C, nor made any project in that language. That said, the code works, but I highly doubt the quality of it.

# Why

Written for : https://blog.urpagin.net/coding-a-minecraft-honeypot/

Why did I write a blog post? Because I wanted to; there is no other reason.
