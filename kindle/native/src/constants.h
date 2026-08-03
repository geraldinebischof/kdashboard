#ifndef KINDLE_DASHBOARD_CONSTANTS_H
#define KINDLE_DASHBOARD_CONSTANTS_H

// Shared compile-time constants for the Kindle dashboard. Header-only:
// `inline constexpr` gives a single definition across every translation unit
// without the internal-linkage duplication of the old anonymous-namespace form.

inline constexpr const char* kDefaultUrl = "";
inline constexpr const char* kDefaultEventsUrl = "";
inline constexpr const char* kDefaultToggleUrl = "";
inline constexpr const char* kDefaultCache = "/mnt/us/documents/kindle-dashboard-data.json";
inline constexpr const char* kRecipeAssetsPath = "/mnt/us/extensions/kindle-dashboard/assets/recipes";
inline constexpr const char* kRecipeAssetsLocalPath = "kindle/kual/kindle-dashboard/assets/recipes";
inline constexpr const char* kCookbookIconPath = "/mnt/us/extensions/kindle-dashboard/assets/cookbook-icon.pgm";
inline constexpr const char* kCookbookIconLocalPath = "kindle/kual/kindle-dashboard/assets/cookbook-icon.pgm";
inline constexpr int kDefaultIntervalSeconds = 3600;
inline constexpr const char* kDefaultSleepWindow = "off";
inline constexpr long kMaxDashboardPayloadBytes = 512 * 1024;
inline constexpr int kScreenColumns = 40;
inline constexpr int kMaxRows = 28;
inline constexpr int kCardInnerWidth = 36;
inline constexpr int kMaxLists = 4;
inline constexpr int kMaxItems = 128;
inline constexpr int kMaxRecipes = 12;
inline constexpr int kMaxRecipeIngredients = 8;
inline constexpr int kBitmapFallbackWidth = 760;
inline constexpr int kBitmapFallbackHeight = 1024;
inline constexpr int kKindleStatusBarHeight = 66;
inline constexpr int kPageFooterHeight = 52;

#endif  // KINDLE_DASHBOARD_CONSTANTS_H
