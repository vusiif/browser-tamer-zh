#include "url_pipeline_step.h"
#include "i18n.h"

namespace bt {
    std::string url_pipeline_step::to_string(url_pipeline_step_type type) {
        switch(type) {
            case url_pipeline_step_type::rule_matcher: return tr("Match rule");
            case url_pipeline_step_type::find_replace: return tr("Find/replace");
            case url_pipeline_step_type::o365: return tr("Unwrap Office 365");
            case url_pipeline_step_type::unshortener: return tr("Un-shorten");
            case url_pipeline_step_type::clearurls: return tr("ClearURLs");
            case url_pipeline_step_type::script: return tr("Script");
            default: return tr("unknown");
        }
    }
}
