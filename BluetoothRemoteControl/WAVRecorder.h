#pragma once

#include "Module.h"

namespace WPEFramework {

namespace WAV {

class EXTERNAL Recorder {
public:
    enum codec {
        PCM = 1,
        ADPCM 
    };

public:
    Recorder(const Recorder&) = delete;
    Recorder& operator= (const Recorder&) = delete;

    Recorder() 
        : _file() 
        , _bitsPerSample(16) {
    }
    ~Recorder() {
    }

public:
    bool IsOpen() const {
        return (_file.IsOpen());
    }
    uint32_t Open(const string& fileName, const codec type, const uint8_t channels, const uint32_t sampleRate, const uint8_t bitsPerSample) {
        uint32_t result = Core::ERROR_UNAVAILABLE;

        _file = Core::File(fileName, false);

        if (_file.Create() == true) {
            _file.Write(reinterpret_cast<const uint8_t*>(_T("RIFF")), 4);
            _file.Write(reinterpret_cast<const uint8_t*>(_T("    ")), 4);
            _file.Write(reinterpret_cast<const uint8_t*>(_T("WAVE")), 4);
            _file.Write(reinterpret_cast<const uint8_t*>(_T("fmt ")), 4);
            Store<uint32_t>(bitsPerSample);         /* SubChunk1Size is 16 */
            Store<uint16_t>(type);   
            Store<uint16_t>(channels);   
            Store<uint32_t>(sampleRate);   
            Store<uint32_t>(sampleRate * channels * bitsPerSample);   
            Store<uint16_t>(channels * bitsPerSample);   
            Store<uint16_t>(bitsPerSample);   
            _file.Write(reinterpret_cast<const uint8_t*>(_T("data")), 4);
            _file.Write(reinterpret_cast<const uint8_t*>(_T("    ")), 4);
            _bitsPerSample = bitsPerSample;
            ASSERT (_bitsPerSample >= 1);
            result = Core::ERROR_NONE;
        }
        return (result);
    }
    void Close() {
        if (_file.IsOpen() == true) {
            _file.Position(false, 4);
            Store<uint32_t>(_file.Size() - 8);
            _file.Position(false, 40);
            Store<uint32_t>((_file.Size() - 44) / ((_bitsPerSample + 7) / 8) );
            _file.Close();
        }
    }
    void Write (const uint16_t length, const uint8_t data[]) {

        _file.Write(data, length);
    }

private:
    template<typename TYPE>
    void Store(const TYPE value) {
        TYPE store = value;
        for (uint8_t index = 0; index < sizeof(TYPE); index++) {
            uint8_t byte = (store & 0xFF);
            _file.Write(&byte, 1);
            store = (store >> 8);
        }
    }

private:
    Core::File _file;
    uint8_t _bitsPerSample;
};
 
} } // namespace WPEFramework::WAV