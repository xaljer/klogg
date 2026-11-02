#include "encdecoders.h"
#include "encoding.h"
#include "encodingdetector.h"

#include <iconv.h>

#include <string>
#include <vector>

namespace klogg {
namespace enc {

namespace {

class IconvDecoder : public AbstractDecoder {
public:
    explicit IconvDecoder( const std::string& iconvName )
        : iconv_( iconv_open( "UTF-8", iconvName.c_str() ) )
    {
    }

    ~IconvDecoder() override
    {
        if ( iconv_ != reinterpret_cast<iconv_t>( -1 ) ) {
            iconv_close( iconv_ );
        }
    }

    std::string decode( const char* data, int len ) override
    {
        if ( len <= 0 || iconv_ == reinterpret_cast<iconv_t>( -1 ) ) {
            return {};
        }

        std::vector<char> output( static_cast<size_t>( len ) * 4 + 16 );
        char* inputPtr = const_cast<char*>( data );
        size_t inputLeft = static_cast<size_t>( len );
        char* outputPtr = output.data();
        size_t outputLeft = output.size();

        const auto result = iconv( iconv_, &inputPtr, &inputLeft, &outputPtr, &outputLeft );
        if ( result == static_cast<size_t>( -1 ) ) {
            return {};
        }

        return std::string( output.data(), output.size() - outputLeft );
    }

private:
    iconv_t iconv_;
};

} // namespace

std::shared_ptr<AbstractDecoder> makeLegacyDecoder( const Encoding& encoding )
{
    if ( encoding.iconv.empty() ) {
        return nullptr;
    }

    return std::make_shared<IconvDecoder>( encoding.iconv );
}

} // namespace enc
} // namespace klogg
