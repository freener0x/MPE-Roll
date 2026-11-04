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

    /** Where the update check asks, when it's turned on or asked to (UpdateCheck.h): GitHub's API
        for the latest release, and the latest release's page, whose redirect names its tag. */
    inline constexpr const char* latestReleaseApi = "https://api.github.com/repos/freener0x/MPE-Roll/releases/latest";
    inline constexpr const char* latestRelease = "https://github.com/freener0x/MPE-Roll/releases/latest";
}
