#include <iostream>
#include <string>
#include <cstdlib>
#include <cstdint>

extern "C" int run_timing_benchmark(const uint32_t m, const float alpha);

#if defined(__linux__)

    extern "C" int run_performance_counters(const uint32_t m, const float alpha);

    void pin_to_cpu(int cpu_id) {
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        CPU_SET(cpu_id, &cpu_set);
        int rv= sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set);
        if(rv < 0){
            std::cout << "Failed to pin to cpu" << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    
#endif

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <mode> <matrix_size> <alpha>" << std::endl;
        std::cerr << "'perf' or 'time'" << std::endl;
        std::cerr << "matrix_size" << std::endl;
        std::cerr << "alpha" << std::endl;
        exit(EXIT_FAILURE);
    }
    
    #if defined(__linux__)
        pin_to_cpu(0);
    #endif

    std::string mode= argv[1];
    uint32_t matrix_size= std::atoi(argv[2]);
    float alpha= std::atof(argv[3]);

    if (mode == "perf") {
        #if defined(__linux__)
            return run_performance_counters(matrix_size, alpha);
        #endif
    } 
    else if (mode == "time") {
        return run_timing_benchmark(matrix_size, alpha);
    }else{
        std::cerr << "Mode " << mode << " Does Not Exist" << std::endl;
        exit(EXIT_FAILURE);
    }
}