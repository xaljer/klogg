#include "encdecoders.h"
#include "encoding.h"
#include "encodingdetector.h"

#include <windows.h>

#include <string>

namespace klogg {
namespace enc {

namespace {

class WinCodePageDecoder : public AbstractDecoder {
public:
    explicit WinCodePageDecoder( unsigned codepage )
        : codepage_( codepage )
    {
    }

    std::string decode( const char* data, int len ) override
    {
        if ( len <= 0 ) {
            return {};
        }

        const int wideLength = MultiByteToWideChar( codepage_, 0, data, len, nullptr, 0 );
        if ( wideLength <= 0 ) {
            return {};
        }

        std::wstring wideBuffer( wideLength, L'\0' );
        MultiByteToWideChar( codepage_, 0, data, len, wideBuffer.data(), wideLength );

        const int utf8Length = WideCharToMultiByte( CP_UTF8, 0, wideBuffer.data(), wideLength,
                                                    nullptr, 0, nullptr, nullptr );
        if ( utf8Length <= 0 ) {
            return {};
        }

        std::string out( static_cast<size_t>( utf8Length ), '\0' );
        WideCharToMultiByte( CP_UTF8, 0, wideBuffer.data(), wideLength, out.data(), utf8Length,
                             nullptr, nullptr );
        return out;
    }

private:
    unsigned codepage_;
};

} // namespace

std::shared_ptr<AbstractDecoder> makeLegacyDecoder( const Encoding& encoding )
{
    if ( encoding.codepage == 0 ) {
        return nullptr;
    }

    return std::make_shared<WinCodePageDecoder>( encoding.codepage );
}

} // namespace enc
} // namespace klogg
