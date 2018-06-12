/**
 * KeywordDetection.cpp
 *
 * @author Kevin Midkiff (kevin.midkiff@intel.com)
 *
 * TODO: Add Intel copyright
 */

#include "Hardware/KeywordDetection.h"

namespace alexaClientSDK {
namespace kwd {

std::unique_ptr<KeywordDetection> KeywordDetection::create(
        int begin, int end, std::string keyword) 
{
    return std::unique_ptr<KeywordDetection>(
            new KeywordDetection(begin, end, keyword));
}

KeywordDetection::KeywordDetection(int begin, int end, std::string keyword, ESPData espdata) :
    m_begin(begin), m_end(end), m_keyword(keyword), m_espData(espdata)
{}

int KeywordDetection::getBegin() {
    return m_begin;
}

int KeywordDetection::getEnd() {
    return m_end;
}

std::string KeywordDetection::getKeyword() {
    return m_keyword;
}
ESPData KeywordDetection::getESPData() {
    return m_espData;
}

} // kwd
} // alexaClientSdk
