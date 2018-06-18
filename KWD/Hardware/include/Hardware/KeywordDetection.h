/**
 * KeywordDetection.h
 *
 * @author Kevin Midkiff (kevin.midkiff@intel.com)
 *
 * TODO: Add Intel copyright
 */

#ifndef ALEXA_CLIENT_SDK_KWD_KEYWORD_DETECTION_H_
#define ALEXA_CLIENT_SDK_KWD_KEYWORD_DETECTION_H_

#include <string>
#include <memory>
#include <AIP/ESPData.h>

namespace alexaClientSDK {
namespace kwd {

/**
 * Representation of a keyword detection.
 */
class KeywordDetection  {
public:

    /**
     * Creates a new pointer to a KeywordDetection.
     *
     * @param begin Index where the keyword begins
     * @param end Index where the keyword ends
     * @param keyword The keyword that was recognized
     * @return @c KeywordDetection, else nullptr otherwise
     */
    static std::unique_ptr<KeywordDetection> create(
            int begin, int end, std::string keyword, capabilityAgents::aip::ESPData espData);

    /// Get the beginning index
    int getBegin();
    
    /// Get the end index
    int getEnd();

    /// Get the keyword that was detected
    std::string getKeyword();
    capabilityAgents::aip::ESPData getESPData();

private:
    /**
     * Constructor.
     *
     * @param begin Index where the keyword begins
     * @param end Index where the keyword ends
     * @param keyword The keyword that was recognized
     */
    KeywordDetection(int begin, int end, std::string keyword, capabilityAgents::aip::ESPData esp);
    
    int m_begin;
    int m_end;
    std::string m_keyword;
    capabilityAgents::aip::ESPData m_espdata; 
};

} // kwd
} // alexaClientSdk

#endif // ALEXA_CLIENT_SDK_KWD_KEYWORD_DETECTION_H_
