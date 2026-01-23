#pragma once

#include <Windows.h>
#include <nvEncodeAPI.h>

#include <GL/gl.h>
#include <cuda.h>
#include <cuda_gl_interop.h>

#include <fstream>
#include <string>

class Encoder {
public:
    Encoder();
    ~Encoder();
    void encode(const char* input, char* output);
	void initializeEncoder();

    void createSession(CUcontext cudaContext);

    void createEncoder(uint32_t width, uint32_t height, uint32_t bitrate, uint32_t frameRate);
    bool mapInput(GLuint textureId, uint32_t width, uint32_t height);
	void unmapInput();
    void processTextureWithNvenc();
    void openOutputFile(const std::string &filename);

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
};
