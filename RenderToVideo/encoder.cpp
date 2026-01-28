#include "encoder.h"
#include <windows.h>
#include <iostream>

// Add the correct include path for CUDA headers
#include <cuda_runtime.h>
#include <vector>
#include <cstring>

namespace {
    int GetCapabilityValue(NV_ENCODE_API_FUNCTION_LIST& nvenc, void* encoder, GUID guidCodec, NV_ENC_CAPS capsToQuery)
    {
        if (!encoder)
        {
            return 0;
        }
        NV_ENC_CAPS_PARAM capsParam = { NV_ENC_CAPS_PARAM_VER };
        capsParam.capsToQuery = capsToQuery;
        int v;
        nvenc.nvEncGetEncodeCaps(encoder, guidCodec, &capsParam, &v);
        return v;
    }

    // Helper to find NAL units in Annex B format
    void extract_sps_pps(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
        size_t i = 0;
        while (i + 4 < size) {
            // Find start code
            if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                size_t nal_start = i + 4;
                uint8_t nal_type = data[nal_start] & 0x1F;
                // Find next start code
                size_t next = nal_start;
                while (next + 4 < size &&
                    !(data[next] == 0x00 && data[next + 1] == 0x00 && data[next + 2] == 0x00 && data[next + 3] == 0x01)) {
                    ++next;
                }
                size_t nal_end = next;
                // If SPS or PPS, copy to output
                if (nal_type == 7 || nal_type == 8) {
                    out.insert(out.end(), &data[i], &data[nal_end]);
                }
                i = nal_end;
            }
            else {
                ++i;
            }
        }
    }

    // Call this after encoding your first keyframe (IDR)
    void set_sps_pps_extradata(AVStream* stream, const uint8_t* data, size_t size) {
        std::vector<uint8_t> sps_pps;
        extract_sps_pps(data, size, sps_pps);
        if (!sps_pps.empty()) {
            stream->codecpar->extradata = (uint8_t*)av_malloc(sps_pps.size() + AV_INPUT_BUFFER_PADDING_SIZE);
            memcpy(stream->codecpar->extradata, sps_pps.data(), sps_pps.size());
            memset(stream->codecpar->extradata + sps_pps.size(), 0, AV_INPUT_BUFFER_PADDING_SIZE);
            stream->codecpar->extradata_size = (int)sps_pps.size();
        }
    }

}

Encoder::Encoder(FILE* ffmpeg_stream)
	: ffmpeg_stream(ffmpeg_stream)
{
}

Encoder::~Encoder() {
	destroyOutputBitstreamBuffer();
    if (nvencDll) {
        FreeLibrary(nvencDll);
        nvencDll = nullptr;
    }
    closeOutputFile();
}

void Encoder::initializeEncoder()
{
    nvencDll = LoadLibraryA("nvEncodeAPI64.dll");
    if (!nvencDll) {
        std::cerr << "Failed to load nvEncodeAPI64.dll" << std::endl;
        return;
    }

    NvEncodeAPICreateInstance = reinterpret_cast<PFN_NvEncodeAPICreateInstance>(
        GetProcAddress(nvencDll, "NvEncodeAPICreateInstance"));
    if (!NvEncodeAPICreateInstance) {
        std::cerr << "Failed to get NvEncodeAPICreateInstance" << std::endl;
        FreeLibrary(nvencDll);
        nvencDll = nullptr;
        return;
    }

    nvenc.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    NVENCSTATUS status = NvEncodeAPICreateInstance(&nvenc);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "NvEncodeAPICreateInstance failed: " << status << std::endl;
        FreeLibrary(nvencDll);
        nvencDll = nullptr;
    }
}

void Encoder::createSession(CUcontext cudaContext) {
    // Open an encoding session with CUDA
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS openParams = {};
    openParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    openParams.deviceType = NV_ENC_DEVICE_TYPE_CUDA; // Use CUDA device
    openParams.device = cudaContext; // Pass the CUDA context
    openParams.apiVersion = NVENCAPI_VERSION;

    NVENCSTATUS status = nvenc.nvEncOpenEncodeSessionEx(&openParams, &encoderSession);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "Failed to open NVENC session: " << status << std::endl;
        throw std::runtime_error("Failed to create NVENC session");
    }

    std::cout << "NVENC session created successfully!" << std::endl;
}

void Encoder::createEncoder(uint32_t width, uint32_t height, uint32_t bitrate, uint32_t frameRate) {
    // Ensure the encoder session is created
    if (!encoderSession) {
        throw std::runtime_error("Encoder session is not created. Call createSession() first.");
    }

    // Initialize encoder parameters
    NV_ENC_INITIALIZE_PARAMS initParams = {};
    NV_ENC_CONFIG encodeConfig = {};

    initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initParams.encodeConfig = &encodeConfig;
    initParams.encodeWidth = width;
    initParams.encodeHeight = height;
    initParams.darWidth = width;
    initParams.darHeight = height;
    initParams.frameRateNum = 30;
    initParams.frameRateDen = 1;
    initParams.enablePTD = 1;
    initParams.encodeGUID = NV_ENC_CODEC_H264_GUID; // Use H.264 codec
    initParams.presetGUID = NV_ENC_PRESET_P5_GUID; // Use default preset
	initParams.tuningInfo = NV_ENC_TUNING_INFO_HIGH_QUALITY; // High quality tuning

    NVENCSTATUS status;
    NV_ENC_PRESET_CONFIG presetConfig = { NV_ENC_PRESET_CONFIG_VER, 0, { NV_ENC_CONFIG_VER } };
    status = nvenc.nvEncGetEncodePresetConfigEx(encoderSession, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P5_GUID,
        NV_ENC_TUNING_INFO_HIGH_QUALITY, &presetConfig);

    presetConfig.presetCfg.profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
    presetConfig.presetCfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
    presetConfig.presetCfg.rcParams.constQP.qpIntra = 28;
    presetConfig.presetCfg.rcParams.constQP.qpInterP = 31;
    presetConfig.presetCfg.rcParams.constQP.qpInterB = 31;
    presetConfig.presetCfg.gopLength = (uint32_t)frameRate * 2;        // or keep as preset
    presetConfig.presetCfg.frameIntervalP = 1;               // single ref interval

    memcpy(initParams.encodeConfig, &presetConfig.presetCfg, sizeof(NV_ENC_CONFIG));

    // Initialize the encoder
    status = nvenc.nvEncInitializeEncoder(encoderSession, &initParams);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "Failed to initialize NVENC encoder: " << status << std::endl;
        throw std::runtime_error("Failed to initialize NVENC encoder");
    }

    std::cout << "NVENC encoder initialized successfully!" << std::endl;

	createOutputBitstreamBuffer();
}

bool Encoder::mapInput(int idx, uint32_t width, uint32_t height)
{
    // Map the resource for access by CUDA
    auto cuErr = cudaGraphicsMapResources(1, &cudaResources[idx], 0);
    if (cuErr != cudaSuccess) {
        std::cerr << "cudaGraphicsMapResources failed: " << cudaGetErrorString(cuErr) << std::endl;
        cudaGraphicsUnregisterResource(cudaResources[idx]);
        return false;
    }

    // Get the CUDA array from the mapped resource
    cudaArray_t cuArray = nullptr;
    cuErr = cudaGraphicsSubResourceGetMappedArray(&cuArray, cudaResources[idx], 0, 0);
    if (cuErr != cudaSuccess) {
        std::cerr << "cudaGraphicsSubResourceGetMappedArray failed: " << cudaGetErrorString(cuErr) << std::endl;
        cudaGraphicsUnmapResources(1, &cudaResources[idx], 0);
        cudaGraphicsUnregisterResource(cudaResources[idx]);
        return false;
    }

    // Register the CUDA array with NVENC
    regRes.version = NV_ENC_REGISTER_RESOURCE_VER;
    regRes.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDAARRAY;
    regRes.resourceToRegister = cuArray;
    regRes.width = width;
    regRes.height = height;
    regRes.pitch = 0;
    regRes.bufferFormat = NV_ENC_BUFFER_FORMAT_ABGR; // Use the format matching your OpenGL texture
    regRes.bufferUsage = NV_ENC_INPUT_IMAGE;

    NVENCSTATUS status = nvenc.nvEncRegisterResource(encoderSession, &regRes);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "nvEncRegisterResource failed: " << status << std::endl;
        cudaGraphicsUnmapResources(1, &cudaResources[idx], 0);
        cudaGraphicsUnregisterResource(cudaResources[idx]);
        return false;
    }

    // Map the registered resource to an NVENC input buffer
    mapInputRes.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    mapInputRes.registeredResource = regRes.registeredResource;

    status = nvenc.nvEncMapInputResource(encoderSession, &mapInputRes);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "nvEncMapInputResource failed: " << status << std::endl;
        nvenc.nvEncUnregisterResource(encoderSession, regRes.registeredResource);
        cudaGraphicsUnmapResources(1, &cudaResources[idx], 0);
        cudaGraphicsUnregisterResource(cudaResources[idx]);
        return false;
    }

    return true;
}

void Encoder::unmapInput(int idx)
{
    // Cleanup: unmap and unregister resources after encoding
    nvenc.nvEncUnmapInputResource(encoderSession, mapInputRes.mappedResource);
    nvenc.nvEncUnregisterResource(encoderSession, regRes.registeredResource);
    cudaGraphicsUnmapResources(1, &cudaResources[idx], 0);
}

bool Encoder::processTextureWithNvenc()
{
    NV_ENC_PIC_PARAMS picParams = {};
    picParams.version = NV_ENC_PIC_PARAMS_VER;
    picParams.inputBuffer = mapInputRes.mappedResource;
    picParams.bufferFmt = NV_ENC_BUFFER_FORMAT_ABGR;
    picParams.inputWidth = regRes.width;
    picParams.inputHeight = regRes.height;
    picParams.outputBitstream = outputBitstreamBuffer;
    picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    NVENCSTATUS status = nvenc.nvEncEncodePicture(encoderSession, &picParams);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "Failed to encode picture: " << status << std::endl;
        return false;
    }

    // Lock the bitstream to access encoded data
    NV_ENC_LOCK_BITSTREAM lockBitstreamData = {};
    lockBitstreamData.version = NV_ENC_LOCK_BITSTREAM_VER;
    lockBitstreamData.outputBitstream = outputBitstreamBuffer;
    lockBitstreamData.doNotWait = false;
    status = nvenc.nvEncLockBitstream(encoderSession, &lockBitstreamData);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "Failed to lock bitstream: " << status << std::endl;
        return false;
    }

    // Write the encoded data to file
    if (lockBitstreamData.bitstreamSizeInBytes > 0) {
        bool keyframe = (lockBitstreamData.pictureType == NV_ENC_PIC_TYPE_IDR);
        //writeFrameToMkv(lockBitstreamData.bitstreamBufferPtr, lockBitstreamData.bitstreamSizeInBytes, keyframe);
		//writeRawFrame(lockBitstreamData.bitstreamBufferPtr, lockBitstreamData.bitstreamSizeInBytes);
        fwrite(lockBitstreamData.bitstreamBufferPtr, 1, lockBitstreamData.bitstreamSizeInBytes, ffmpeg_stream);
    }

    // Unlock the bitstream
    nvenc.nvEncUnlockBitstream(encoderSession, outputBitstreamBuffer);
	return true;
}

void Encoder::createOutputBitstreamBuffer()
{
    NV_ENC_CREATE_BITSTREAM_BUFFER createBitstreamBuffer = {};
    createBitstreamBuffer.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;

    NVENCSTATUS status = nvenc.nvEncCreateBitstreamBuffer(encoderSession, &createBitstreamBuffer);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "Failed to create output bitstream buffer: " << status << std::endl;
        throw std::runtime_error("Failed to create output bitstream buffer");
    }

    // Store the created bitstream buffer handle for later use
    outputBitstreamBuffer = createBitstreamBuffer.bitstreamBuffer;
    std::cout << "Output bitstream buffer created successfully!" << std::endl;
}

void Encoder::destroyOutputBitstreamBuffer()
{
    if (outputBitstreamBuffer) {
        NVENCSTATUS status = nvenc.nvEncDestroyBitstreamBuffer(encoderSession, outputBitstreamBuffer);
        if (status != NV_ENC_SUCCESS) {
            std::cerr << "Failed to destroy output bitstream buffer: " << status << std::endl;
        } else {
            std::cout << "Output bitstream buffer destroyed successfully!" << std::endl;
        }
        outputBitstreamBuffer = nullptr;
    }
}

// Call this before encoding starts
void Encoder::openOutputFile(const std::string& filename, int width, int height, int fps) {

    avformat_alloc_output_context2(&fmt_ctx, nullptr, "matroska", filename.c_str());
    if (!fmt_ctx) throw std::runtime_error("Could not allocate output context");

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    codec_ctx->width = width;
    codec_ctx->height = height;
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx->time_base = AVRational{1, fps};
    codec_ctx->bit_rate = 4000000;

    // Open codec to fill extradata
    avcodec_open2(codec_ctx, codec, nullptr);

    // Create stream and copy parameters
    video_stream = avformat_new_stream(fmt_ctx, codec);
    video_stream->time_base = AVRational{ 1, fps };
    avcodec_parameters_from_context(video_stream->codecpar, codec_ctx);
    video_stream->codecpar->codec_tag = 0;

    // Clean up
    avcodec_free_context(&codec_ctx);

    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0)
            throw std::runtime_error("Could not open output file");
    }

    frame_no = 0;
}

// Call this after encoding ends
void Encoder::closeOutputFile() {
    if (fmt_ctx) {
        av_write_trailer(fmt_ctx);
        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&fmt_ctx->pb);
        }
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
        video_stream = nullptr;
    }
}

// Call this after each frame is encoded (in processTextureWithNvenc)
void Encoder::writeFrameToMkv(const void* data, size_t size, bool keyframe) {
    static bool first_key_frame = true;

    if (first_key_frame && keyframe) {
        set_sps_pps_extradata(video_stream, static_cast<const uint8_t*>(data), size);
        if (avformat_write_header(fmt_ctx, nullptr) < 0)
            throw std::runtime_error("Error occurred when writing header");
        first_key_frame = false;
	}

	std::cout << "Writing frame, size: " << size << ", keyframe: " << keyframe << ", pts: " << frame_no << std::endl;
    if (!fmt_ctx || !video_stream) return;

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) return;
	//const int64_t pts = (frame_no * 1000) / 25; // assuming 25 fps
    pkt->data = (uint8_t*)data;
    pkt->size = static_cast<int>(size);
    pkt->stream_index = video_stream->index;
    pkt->pts = frame_no;
    pkt->dts = frame_no;
    pkt->duration = 1; //1000 / 25;
    pkt->flags = keyframe ? AV_PKT_FLAG_KEY : 0;
    pkt->pos = -1;

    av_interleaved_write_frame(fmt_ctx, pkt);
    av_packet_free(&pkt);
    frame_no++;
}

void Encoder::writeRawFrame(const void* data, size_t size)
{
    if (outputRawFile.is_open()) {
        outputRawFile.write(static_cast<const char*>(data), size);
    }
}

void Encoder::setOutputFile(const std::string& filename)
{
    outputRawFile.open(filename, std::ios::binary);
    if (!outputRawFile.is_open()) {
        throw std::runtime_error("Failed to open output file: " + filename);
    }
}

void Encoder::closeOutputRawFile()
{
    if (outputRawFile.is_open()) {
        outputRawFile.close();
    }
}

void Encoder::registerCudaResource(GLuint textureId, uint32_t width, uint32_t height)
{
        cudaGraphicsResource* cudaRes = nullptr;
        cudaError_t cuErr = cudaGraphicsGLRegisterImage(
            &cudaRes,
            textureId,
            GL_TEXTURE_2D,
            cudaGraphicsRegisterFlagsReadOnly
        );
        if (cuErr != cudaSuccess) {
            std::cerr << "cudaGraphicsGLRegisterImage failed for texture id: " << textureId << " : " << cudaGetErrorString(cuErr) << std::endl;
        }
        cudaResources.push_back(cudaRes);
}
