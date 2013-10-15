/*

switches:
- sending vs receiving
- vary: context takeover, memory bits, window bits

stats:
- frame bytes
- payload bytes raw
- payload bytes wire

table of window bits cross memory bits x2 (with and without context takeover)

*/

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "zlib.h"

class pod_buffer {
public:
    pod_buffer() : m_cursor(0), m_capacity(0) {}

    void resize(size_t new_size) {
        if (new_size <= m_capacity) {
            return;
        }

        m_buf.reset(new unsigned char[new_size]);
        m_capacity = new_size;
    }

    size_t capacity() const {
        return m_capacity;
    }

    unsigned char * first_avail() {
        return m_buf.get()+m_cursor;
    }
    
    size_t avail() const {
        return m_capacity - m_cursor;
    }

    size_t cursor() const {
        return m_cursor;
    }

    void set_cursor(size_t cursor) {
        m_cursor = cursor;
    }
    void adv_cursor(size_t cursor) {
        m_cursor += cursor;
    }
private:
    size_t m_cursor;
    size_t m_capacity;
    std::unique_ptr<unsigned char> m_buf;
};

size_t frame_overhead(bool masked, size_t payload_size) {
    size_t size = (masked ? 4 : 0);

    if (payload_size <= 125) {
        size += 2;
    } else if (payload_size <= 0xffff) {
        size += 4;
    } else {
        size += 8;
    }
    return size;
}

struct line_result {
    size_t payload_size;
    size_t frame_overhead;
    size_t compressed_size;
    double ratio;
    double elapsed_seconds;
    // test length in sec
};

struct test_result {
    // test settings
    bool is_server;
    bool sending;
    bool context_takeover;
    int speed_level;
    int window_bits;
    int memory_level;
    // test results
    std::vector<line_result> line_results;

    size_t total_payload;
    size_t total_frame_overhead;
    size_t total_compressed_size;
    double total_ratio;
    double total_elapsed_seconds;

    // memory stats
    size_t mem_usage;
    size_t mem_usage_inflate_32;
    size_t mem_usage_inflate_64;

    void calc_stats() {
        total_payload = 0;
        total_frame_overhead = 0;
        total_compressed_size = 0;
        total_ratio = 0;

        for (auto & lr : line_results) {
            total_payload += lr.payload_size;
            total_frame_overhead += lr.frame_overhead;
            total_compressed_size += lr.compressed_size;
            total_elapsed_seconds += lr.elapsed_seconds;
        }

        total_ratio = double(total_compressed_size) / double(total_payload);

        if (sending) {
            mem_usage = (1 << (window_bits + 2)) + (1 << (memory_level + 9));
            mem_usage_inflate_32 = (1 << window_bits) + 1440*2*4;
            mem_usage_inflate_64 = (1 << window_bits) + 1440*2*8;
        } else {
            mem_usage = (1 << window_bits) + 1440*2*sizeof(int);
            mem_usage_inflate_32 = 0;
            mem_usage_inflate_64 = 0;
        }
    }
};

test_result deflate_test(std::istream & input, test_result r) {
    z_stream zlib_state;
    const size_t buffer_size = 16384;
    unsigned char buffer[buffer_size];

    pod_buffer out_buf;

    // error checking for parameters

    int ret = deflateInit2(
        &zlib_state,
        r.speed_level,
        Z_DEFLATED,
        -1*r.window_bits,
        r.memory_level,
        Z_DEFAULT_STRATEGY
    );

    if (ret != Z_OK) {
        // handle error
    }

    int flush = (r.context_takeover ? Z_SYNC_FLUSH : Z_FULL_FLUSH);


    std::string line;
    while (std::getline(input, line)) {
        line_result lr;
        lr.payload_size = line.size();
        lr.frame_overhead = frame_overhead(!r.is_server,line.size());

        // compress
        if (line.empty()) {
            // compressed value will be 2 bytes
            lr.compressed_size = 2;
            lr.ratio = 2.0;
            r.line_results.push_back(lr);
            continue;
        }

        zlib_state.avail_in = line.size();
        zlib_state.next_in = reinterpret_cast<unsigned char *>(const_cast<char *>(line.data()));

        size_t est_size = deflateBound(&zlib_state,line.size());
        out_buf.resize(est_size);
        out_buf.set_cursor(0);

        zlib_state.avail_out = out_buf.avail();
        zlib_state.next_out = out_buf.first_avail();

        // start timer
        std::chrono::time_point<std::chrono::high_resolution_clock> start, end;

        start = std::chrono::high_resolution_clock::now();

        deflate(&zlib_state, flush);

        // end timer
        end = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> elapsed_seconds = end-start;
        lr.elapsed_seconds = elapsed_seconds.count();

        out_buf.adv_cursor(out_buf.avail() - zlib_state.avail_out);
        
        if (out_buf.avail() == 0) {
            // handle error
        }

        lr.compressed_size = out_buf.cursor()-4;
        // record time

        lr.ratio = double(lr.compressed_size) / double(lr.payload_size);
        r.line_results.push_back(lr);
    }

    return r;
}

int main(int argc, char * argv[]) {
    // some places to put stats
    test_result r;

    r.is_server = true;
    r.sending = true;
    r.context_takeover = true;
    r.speed_level = 6;
    r.window_bits = 15;
    r.memory_level = 8;
    
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);

        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: "
                      << "wspmcestats [parameter1=val1, [parameter2=val2]]\n\n"
                      << "Pass data in via standard input. wspmcestats will simulate a WebSocket\n"
                      << "connection using the parameters defined below. One line of input\n"
                      << "represents one websocket message. Stats about the speed, memory usage,\n"
                      << "and compression ratio will be printed at the end.\n\n"
                      << "Optional parameters: (usage key=val, in any combination, in any order)\n"
                      << "  server: [true,false]; Default true; \n"
                      << "    Simulate a server (vs client). Affects frame overhead stats.\n\n"
                      << "  sending: [true,false]; Default true; \n"
                      << "    Simulate sending (vs receiving). Affects memory usage stats.\n\n"
                      << "  context_takeover: [true,false]; Default true; \n"
                      << "    Reuse compression context between messages. A value of false is\n"
                      << "    equivilent to negotiating the permessage-deflate setting of \n"
                      << "    *_no_context_takeover. If this value is true a separate compression\n"
                      << "    context must be maintained for each connection. \n\n"
                      << "  speed_level: [0...9]; Default 6; \n"
                      << "    A tuning parameter that trades compression quality vs CPU usage.\n"
                      << "    A value of 0 indicates no compression at all. This value may be\n"
                      << "    unilaterally set by a WebSocket endpoint without negotiation.\n\n"
                      << "  window_bits: [8-15]; Default 15; \n"
                      << "    Base 2 logarithm of the size to use for the LZ77 sliding window.\n"
                      << "    Higher values use more memory but provide better compression. This\n"
                      << "    value must be negotiated. A stream compressed with n bits can be\n"
                      << "    decompressed only by an endpoint that uses at least that many. Not\n"
                      << "    all WebSocket endpoints will support negotiating this parameter.\n\n"
                      << "  memory_level: [1-9]; Default 8; \n"
                      << "    A tuning parameter that trades compression quality vs memory usage.\n"
                      << "    A value of 1 indicates lowest memory usage but worst compression. A\n"
                      << "    value of 9 incidates most memory usage but best compression. This\n"
                      << "    parameter may be set unilaterally without negotiation."
                      << std::endl;
            return 0;
        }

        auto pos = arg.find('=');
        if (pos != std::string::npos) {
            std::string key(arg.begin(),arg.begin()+pos);
            std::string val(arg.begin()+pos+1,arg.end());
            
            if (key == "server") {
                r.is_server = (val == "true" ? true : false);
            } else if (key == "sending") {
                r.sending = (val == "true" ? true : false);
            } else if (key == "context_takeover") {
                r.context_takeover = (val == "true" ? true : false);
            } else if (key == "speed_levels") {
                r.speed_level = atoi(val.c_str()); 
            } else if (key == "window_bits") {
                r.window_bits = atoi(val.c_str()); 
            } else if (key == "memory_level") {
                r.memory_level = atoi(val.c_str()); 
            }
        }
    }
    
    if (r.speed_level > 9 || r.speed_level < 0) {
        std::cout << "Speed level must be between 0 (fastest, no compression) and 9 (slowest, best compression). Default is 6" << std::endl;
        return 1;
    }
    if (r.window_bits > 15 || r.window_bits < 8) {
        std::cout << "Window bits must be between 8 (lower memory usage, worse compression) and 15 (highest memory usage, best compression). Default is 15." << std::endl;
        return 1;
    }
    if (r.memory_level > 9 || r.memory_level < 1) {
        std::cout << "Memory level must be between 1 (lower memory usage, worse compression) and 9 (highest memory usage, best compression). Default is 8." << std::endl;
        return 1;
    }

    r = deflate_test(std::cin, r);

    std::cout << "simulating: " << (r.is_server ? "server " : "client ") 
              << (r.sending ? "sending " : "receiving ") << std::endl;
    std::cout << "settings: context_takeover=" << (r.context_takeover ? "true " : "false ")
              << "speed_level=" << r.speed_level
              << " window_bits=" << r.window_bits
              << " memory_level=" << r.memory_level
              << std::endl << std::endl;
    std::cout << std::left << std::setw(20) <<  "Messages sent: " << r.line_results.size() << std::endl;

    r.calc_stats();

    std::cout << std::left << std::setw(20) << "Source bytes: " << double(r.total_payload)/1000.0 
              << "KB" << std::endl;
    std::cout << std::left << std::setw(20) << "Compressed bytes: " 
              << double(r.total_compressed_size)/1000.0 << "KB" << std::endl;
    std::cout << std::left << std::setw(20) << "Bytes sent on wire: " 
              << double(r.total_compressed_size+r.total_frame_overhead)/1000.0 << "KB" << std::endl;
    std::cout << std::left << std::setw(20) << "Compression ratio: " << r.total_ratio << std::endl;
    std::cout << std::left << std::setw(20) << "Elapsed Time: " << r.total_elapsed_seconds*1000.0
              << "ms" << std::endl;
    std::cout << std::left << std::setw(20) << "Frame overhead: " 
              << double(r.total_frame_overhead) / double(r.total_compressed_size)*100.0 
              << "%" << std::endl << std::endl;

    if (r.sending) {
        std::cout << "Memory used: " << double(r.mem_usage)/1024.0 << "KiB " 
                  << (r.context_takeover ? "per connection" : "total") 
                  << " for compression state." << std::endl;
        std::cout << "Minimum memory required to decompress: " 
                  << double(r.mem_usage_inflate_32)/1024.0 << "KiB (32 bit systems), "
                  << double(r.mem_usage_inflate_64)/1024.0 << "KiB (64 bit systems)"
                  << std::endl;
    } else {
        std::cout << "Memory used: " << double(r.mem_usage)/1024.0 << "KiB " 
                  << (r.context_takeover ? "per connection" : "total") 
                  << " for decompression state." << std::endl;

    }
    return 0;
}
