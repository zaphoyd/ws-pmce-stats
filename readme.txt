WebSocket Per-Message Compress Extension statistics calculator
==============================================================

ws-pmce-stats is a program that performs some basic calculations to 
determine the speed, memory usage, and compression effectiveness of
various input data sets and compression settings.

In particular, it can be used to tune the negotiation parameters of
WebSocket permessage compression extensions based on a set of input
data that roughly matches the types of messages that will be sent
by the application in question.

Build
=====
ws-pmce-stats is written in C++. Its only dependencies are the C++11
standard library and the zlib headers and library. Build examples
on several common platforms follow.

Mac OS X / XCode (clang/llvm)
clang++ -std=c++0x -stdlib=libc++ -o ws-pmce-stats ws-pmce-stats.cpp -lz

Linux / GCC
g++ -std=c++0x -o ws-pmce-stats ws-pmce-stats.cpp -lz

Usage
=====
This information can also be printed by running `ws-pmce-stats --help`

Usage: ws-pmce-stats [parameter1=val1, [parameter2=val2]]

Pass data in via standard input. ws-pmce-stats will simulate a WebSocket
connection using the parameters defined below. One line of input
represents one websocket message. Stats about the speed, memory usage,
and compression ratio will be printed at the end.

Optional parameters: (usage key=val, in any combination, in any order)
  server: [true,false]; Default true; 
    Simulate a server (vs client). Affects frame overhead stats.

  sending: [true,false]; Default true; 
    Simulate sending (vs receiving). Affects memory usage stats.

  context_takeover: [true,false]; Default true; 
    Reuse compression context between messages. A value of false is
    equivilent to negotiating the permessage-deflate setting of 
    *_no_context_takeover. If this value is true a separate compression
    context must be maintained for each connection. 

  speed_level: [0...9]; Default 6; 
    A tuning parameter that trades compression quality vs CPU usage.
    A value of 0 indicates no compression at all. This value may be
    unilaterally set by a WebSocket endpoint without negotiation.

  window_bits: [8-15]; Default 15; 
    Base 2 logarithm of the size to use for the LZ77 sliding window.
    Higher values use more memory but provide better compression. This
    value must be negotiated. A stream compressed with n bits can be
    decompressed only by an endpoint that uses at least that many. Not
    all WebSocket endpoints will support negotiating this parameter.

  memory_level: [1-9]; Default 8; 
    A tuning parameter that trades compression quality vs memory usage.
    A value of 1 indicates lowest memory usage but worst compression. A
    value of 9 incidates most memory usage but best compression. This
    parameter may be set unilaterally without negotiation.

Examples
========

Run with default settings on JSON Chat example data set
`cat datasets/jsonchat.txt | ./ws-pmce-stats`

Change one default setting
`cat datasets/jsonchat.txt | ./ws-pmce-stats context_takeover=false`

Change all default settings
`cat datasets/jsonchat.txt | ./ws-pmce-stats server=false sending=false context_takeover=false windowbits=8 memory_level=1 speed_level=1`

Author & License
================

Written by Peter Thorson (websocket@zaphoyd.com)

BSD Licensed (see source code for full license details)
