#pragma once

#include "config.h"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>

namespace profiling {
    struct ProfileResult {
        std::string name;
        uint64_t start;
        uint64_t end;
        uint64_t thread;
    };

    struct ProfilingSession {
        std::string name;
    };

    class Instrumentor {
    public:
        void BeginSession(const std::string& name)
        {
            m_OutputStream.open("results.tracing.json");
            m_Session = (ProfilingSession) { name };
            m_OutputStream << "[";
        }

        void BeginGpuSession(const std::string *headings, uint32_t count)
        {
            m_GpuOutputStream.open("gpu_results.txt");
            m_GpuHeadings = new std::string[count];
            m_GpuHeadingCount = count;
            for (uint32_t i = 0; i < count; i++) {
                m_GpuHeadings[i] = std::string(headings[i]);
                m_OutputStream << m_GpuHeadings[i] << ", ";
            }
            m_GpuOutputStream << std::endl;
        }

        void EndSession()
        {
            if (!m_OutputStream.is_open()) {
                ERROR("Session was never started");
                return;
            }
            m_OutputStream << "{"
                           << "    \"name\": \"end\","
                           << "    \"cat\": \"special\","
                           << "    \"ph\": \"X\","
                           << "    \"ts\": \"0\","
                           << "    \"dur\": \"0\","
                           << "    \"pid\": \"0000\","
                           << "    \"tid\": \"0\""
                           << "}]";
            m_OutputStream.close();

            if (m_GpuOutputStream.is_open()) {
                m_GpuOutputStream.close();
            }
        }

        void WriteGpuValues(float *valuesNS)
        {
            if (!m_GpuOutputStream.is_open()) {
                ERROR("Writing gpu profile to closed stream");
                return;
            }
            for (uint32_t i = 0; i < m_GpuHeadingCount; i++) {
                m_GpuOutputStream << valuesNS[i] * 1e-6f << ", ";
            }
            m_GpuOutputStream << std::endl;
        }

        void WriteProfile(const ProfileResult& result)
        {
            if (!m_OutputStream.is_open()) {
                ERROR("Writing profile to closed stream");
                return;
            }
            m_OutputStream << "{"
                           << "    \"name\": \"" << result.name << "\","
                           << "    \"cat\": \"default\","
                           << "    \"ph\": \"X\","
                           << "    \"ts\": \"" << result.start << "\","
                           << "    \"dur\": \"" << std::max(result.end - result.start, 1ul) << "\","
                           << "    \"pid\": \"0000\","
                           << "    \"tid\": \"" << result.thread << "\""
                           << "},";
        }

        static Instrumentor& Get() 
        { 
            static Instrumentor instance; 
            return instance; 
        }

    private:
        Instrumentor() {};
        Instrumentor(const Instrumentor&) = delete;
        void operator=(const Instrumentor&) = delete;

        ProfilingSession m_Session;
        uint32_t m_GpuHeadingCount;
        std::string *m_GpuHeadings;
        std::ofstream m_OutputStream;
        std::ofstream m_GpuOutputStream;
    };

    class Timer {
    public:
        Timer(const char* name)
        {
            m_Name = name;
            m_Stopped = false;
            m_StartTimepoint = std::chrono::high_resolution_clock::now();
        }

        ~Timer()
        {
            if (!m_Stopped) Stop();
        }

        void Stop()
        {
            std::chrono::time_point<std::chrono::high_resolution_clock> endTimepoint = std::chrono::high_resolution_clock::now();

            uint64_t start = std::chrono::time_point_cast<std::chrono::microseconds>(m_StartTimepoint).time_since_epoch().count();
            uint64_t end = std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch().count();

            uint32_t thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
            Instrumentor::Get().WriteProfile({ m_Name, start, end, thread });

            m_Stopped = true;
        }

    private:
        const char *m_Name;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_StartTimepoint;
        bool m_Stopped;
    };
}

#ifdef PROFILING
    #define PROFILER_BEGIN_SESSION(name) profiling::Instrumentor::Get().BeginSession(name)
    #define PROFILER_END_SESSION() profiling::Instrumentor::Get().EndSession()
    #define PROFILER_PROFILE_SCOPE(name) profiling::Timer timer##__LINE__(name)
    #define PROFILER_BEGIN_GPU_SESSION(headings, count) profiling::Instrumentor::Get().BeginGpuSession(headings, count)
    #define PROFILER_GPU_VALUES(valuesNS) profiling::Instrumentor::Get().WriteGpuValues(valuesNS)
#else 
    #define PROFILER_BEGIN_SESSION(name)
    #define PROFILER_END_SESSION()
    #define PROFILER_PROFILE_SCOPE(name)
    #define PROFILER_BEGIN_GPU_SESSION(headings, count)
    #define PROFILER_GPU_VALUES(valuesNS)
#endif
