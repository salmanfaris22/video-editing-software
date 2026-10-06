add_library(lectern_media STATIC
    FFmpeg.cpp
    Frame.cpp
    FileSink.cpp
    Muxer.cpp
    VideoEncoder.cpp
    AudioEncoder.cpp
    AudioResampler.cpp
    MediaProbe.cpp
    HardwareCapabilities.cpp
    VideoFramePool.cpp
    VideoReader.cpp
    AudioReader.cpp
    Waveform.cpp
    SalvageRemux.cpp
    JpegDecoder.cpp
)
add_library(Lectern::media ALIAS lectern_media)

target_link_libraries(lectern_media
    PUBLIC
        lectern_core
        FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample)
lectern_configure_target(lectern_media)
