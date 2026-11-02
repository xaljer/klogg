#ifndef KLOGG_ENC_DECODERS_H
#define KLOGG_ENC_DECODERS_H

#include <memory>

namespace klogg {
struct Encoding;
namespace enc {
class AbstractDecoder;
std::shared_ptr<AbstractDecoder> makeLegacyDecoder( const Encoding& );
} // namespace enc
} // namespace klogg

#endif // KLOGG_ENC_DECODERS_H
