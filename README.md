<div align="center">

[![Total Downloads](https://img.shields.io/github/downloads/CapStash/CapStash-Core/total?label=Total%20Downloads&cacheSeconds=300)](https://github.com/CapStash/CapStash-Core/releases)
[![v27.0.0 Downloads](https://img.shields.io/github/downloads/CapStash/CapStash-Core/v27.1.0/total?label=v27.1.0%20Downloads&cacheSeconds=300)](https://github.com/CapStash/CapStash-Core/releases/tag/v27.1.0)
[![Latest Release](https://img.shields.io/github/v/release/CapStash/CapStash-Core?label=Latest%20Release&cacheSeconds=300)](https://github.com/CapStash/CapStash-Core/releases)

</div>

# ☢️ CapStash Core ☢️

Before there were banks, there were bullets.  
Before there were markets, there was barter.
And after the bombs fell, there were Caps.

In the old world, money was numbers on screens, promises in databases, and paper backed by faith in systems too large to fail.

Then the world failed.

When civilization collapsed, people did what they have always done: they adapted. They traded what held value. Food. Water. Ammunition. Medicine. Tools. Fuel. Information. And, over time, one strange relic of the dead world rose above the rest:

the bottle cap.

It was durable. Recognizable. Difficult enough to fake. Small enough to carry. Useless enough in any other context to gain meaning in this one. In the wasteland, value did not come from governments. It came from trust, scarcity, and survival.

CapStash is that idea reborn for the digital wasteland.

CapStash imagines what the currency of the Fallout universe would become if bottle caps evolved beyond pockets, pouches, and rusted vending trays—if they became programmable, mineable, transferable, and verifiable across a decentralized network built to survive the collapse of the old financial world.

This is not just another cryptocurrency with a recycled codebase and a new logo.

CapStash is a digital bottle cap economy:
- mined instead of minted
- verified instead of trusted
- carried by nodes 
- secured by proof-of-work

If Bitcoin is pristine digital gold, CapStash is something far more rugged:

Money for survivors.

Not polished. Not delicate. Not sterile.  
A currency for scavengers, traders, mechanics, prospectors, bunker lords, signal hunters, and anyone stubborn enough to keep building after the world is supposed to be over.

CapStash Core is the terminal through which that world comes alive.

A full node.  
A wallet.  
A miner.  
A blockchain explorer.  
A retro-futurist machine glowing with phosphor light in the dark.

This is what bottle caps become when the wasteland goes online.

---

## What is a Cap?

A Cap is the base unit of the CapStash network.

Every Cap represents a unit of value secured by a living decentralized system of miners, nodes, and users. Like the physical caps of the wasteland, digital Caps are designed around the same core principles:

- recognizable
- countable
- limited by effort
- useful in trade
- trusted because the network says so, not because a central authority does

In a dead world, value belongs to whatever still works.

Caps still work.

---

## Why Caps?

Because bottle caps were never just a joke.

They were one of fiction’s greatest monetary symbols: absurd at first glance, then strangely perfect the longer you think about them. A bottle cap is small, standardized, portable, and culturally sticky. In Fallout, it became money because people believed in it long enough for it to become real.

CapStash takes that same leap.

It asks a simple question:

What if the wasteland had a blockchain?

What if the currency of survivors wasn’t locked in vaults, controlled by institutions, or printed into meaninglessness—but instead emerged from computation, scarcity, and collective agreement?

What if the bottle cap grew teeth?

---

## CapStash Core

CapStash Core is the reference full-node wallet for the CapStash network.

It is built to feel less like a financial app and more like a machine recovered from the ruins:
- terminal-born
- phosphor-lit
- heavy with atmosphere
- engineered to feel like a working artifact from another timeline

It does not just hold the currency.

It inhabits the world the currency belongs to.

---

## The Idea

CapStash is built on a simple premise:

When the old world dies, money does not disappear.  
It mutates into whatever the survivors trust.

In fiction, that was the bottle cap.

In software, that is CapStash.

## Network Traits

- Proof of Work
- 1 Cap per block
- 1 minute target block time
- transaction fees paid to miners
- built-in miner
- built-in explorer
- custom retro-futurist theme system

---

## Connecting to the Network

If your node is having trouble finding peers, or you simply want to bootstrap faster, you can manually connect to known active nodes on the CapStash network.

### Option 1: Add to `capstash.conf`

Add the following lines to your `capstash.conf` file (located in your CapStash data directory):

```
addnode=73.26.110.235:9999
addnode=129.121.76.126:9999
addnode=178.156.177.32:8433
addnode=50.6.6.41:9999
addnode=149.50.101.95:20033
addnode=218.148.222.67:9999
addnode=38.134.40.199:9998
addnode=108.165.12.195:9998
addnode=204.168.162.29:9999
addnode=109.123.241.63:9998
addnode=167.94.7.86:9999
addnode=174.22.111.105:9999
addnode=166.88.160.95:9998
addnode=172.81.100.173:9999
addnode=37.107.6.31:9999
addnode=202.130.201.30:9999
addnode=114.198.50.171:9999
```

Then restart CapStash Core.

### Option 2: Add Live via Console

Open the CapStash Core debug console (`Window → Console`) and paste each line:

```
addnode 73.26.110.235:9999 add
addnode 129.121.76.126:9999 add
addnode 178.156.177.32:8433 add
addnode 50.6.6.41:9999 add
addnode 149.50.101.95:20033 add
addnode 218.148.222.67:9999 add
addnode 38.134.40.199:9998 add
addnode 108.165.12.195:9998 add
addnode 204.168.162.29:9999 add
addnode 109.123.241.63:9998 add
addnode 167.94.7.86:9999 add
addnode 174.22.111.105:9999 add
addnode 166.88.160.95:9998 add
addnode 172.81.100.173:9999 add
addnode 37.107.6.31:9999 add
addnode 202.130.201.30:9999 add
addnode 114.198.50.171:9999 add
```

Or via `capstash-cli`:

```bash
capstash-cli addnode "73.26.110.235:9999" add
```

> **Note:** Nodes added via the console are session-only and will be forgotten on restart. For permanent connections, use the `capstash.conf` method.

---

## Join Vault 1337

Discord: https://discord.gg/8bgFUcFW2T
