#pragma once

#include <Windows.h>
#include <nvEncodeAPI.h>

#include <GL/gl.h>
#include <cuda.h>
#include <cuda_gl_interop.h>

// Add these includes at the top
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <fstream>
#include <string>

class Encoder {
public:
    explicit Encoder(FILE *ffmpeg_stream);
    ~Encoder();
	void initializeEncoder();

    void createSession(CUcontext cudaContext);

    void createEncoder(uint32_t width, uint32_t height, uint32_t bitrate, uint32_t frameRate);
    bool mapInput(GLuint textureId, uint32_t width, uint32_t height);
	void unmapInput();
    bool processTextureWithNvenc();
    void openOutputFile(const std::string &filename, int width, int height, int fps);
    void writeFrameToMkv(const void* data, size_t size, bool keyframe);
    void writeRawFrame(const void* data, size_t size);
    void setOutputFile(const std::string& filename);
    void closeOutputRawFile();
private:
    void createOutputBitstreamBuffer();
	void destroyOutputBitstreamBuffer();
	void closeOutputFile();

private:
    HMODULE nvencDll = nullptr;
    NV_ENCODE_API_FUNCTION_LIST nvenc = {};
    typedef NVENCSTATUS(NVENCAPI* PFN_NvEncodeAPICreateInstance)(NV_ENCODE_API_FUNCTION_LIST*);
    PFN_NvEncodeAPICreateInstance NvEncodeAPICreateInstance = nullptr;
	void* encoderSession = nullptr;
    cudaGraphicsResource* cudaResource = nullptr;
    NV_ENC_REGISTER_RESOURCE regRes = {};
    NV_ENC_MAP_INPUT_RESOURCE mapInputRes = {};
	NV_ENC_OUTPUT_PTR outputBitstreamBuffer = nullptr;
    std::ofstream outputFile;
    std::ofstream outputRawFile;
    AVFormatContext* fmt_ctx = nullptr;
    AVStream* video_stream = nullptr;
    int64_t frame_no = 0;
	FILE* ffmpeg_stream = nullptr;
};
