#pragma once

/** The web addresses MPE Roll reports. (The donation details are in the plugin itself, in
    ui/DonateData.h, so Support works without the website.) */
namespace mperoll::links
{
    inline constexpr const char* repository = "https://github.com/freener0x/MPE-Roll";

    /** The issue tracker. BugReport::issueUrl opens one of its forms, filled in. */
    inline constexpr const char* issues = "https://github.com/freener0x/MPE-Roll/issues";

    /** The website's share section: posts about MPE Roll ready to put on X, Instagram and Facebook.
        (The website's page of its own, share.html, takes this over once it exists.) */
    inline constexpr const char* share = "https://freener0x.github.io/MPE-Roll/#share";
}
