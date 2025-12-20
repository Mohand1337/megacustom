// DesignTokens.h - Centralized design token definitions for MegaCustom
// Generated from ColorThemedTokens.json - Single source of truth for all colors
#ifndef DESIGNTOKENS_H
#define DESIGNTOKENS_H

#include <QColor>

namespace MegaCustom {
namespace DesignTokens {

// Helper to convert ARGB hex to QColor
inline QColor fromArgb(const char* argbHex) {
    // Format: #AARRGGBB
    QString hex(argbHex);
    if (hex.length() == 9 && hex.startsWith('#')) {
        bool ok;
        uint alpha = hex.mid(1, 2).toUInt(&ok, 16);
        uint red = hex.mid(3, 2).toUInt(&ok, 16);
        uint green = hex.mid(5, 2).toUInt(&ok, 16);
        uint blue = hex.mid(7, 2).toUInt(&ok, 16);
        return QColor(red, green, blue, alpha);
    }
    return QColor(argbHex);
}

namespace Light {
    // Background colors
    inline QColor BackgroundBlur() { return fromArgb("#33000000"); }
    inline QColor BackgroundInverse() { return fromArgb("#ff2a2b2c"); }
    inline QColor PageBackground() { return fromArgb("#ffffffff"); }

    // Border colors - using original MEGA red #D90007
    inline QColor BorderBrand() { return fromArgb("#ffD90007"); }
    inline QColor BorderDisabled() { return fromArgb("#ffd8d9db"); }
    inline QColor BorderStrong() { return fromArgb("#ffdcdddd"); }
    inline QColor BorderStrongSelected() { return fromArgb("#ff04101e"); }
    inline QColor BorderSubtle() { return fromArgb("#fff6f6f7"); }
    inline QColor BorderSubtleSelected() { return fromArgb("#ff04101e"); }

    // Brand colors - using original MEGA red #D90007 for consistency
    inline QColor BrandContainerDefault() { return fromArgb("#1aD90007"); }
    inline QColor BrandContainerHover() { return fromArgb("#33D90007"); }
    inline QColor BrandContainerPressed() { return fromArgb("#4dD90007"); }
    inline QColor BrandDefault() { return fromArgb("#ffD90007"); }
    inline QColor BrandHover() { return fromArgb("#ffC00006"); }
    inline QColor BrandOnBrand() { return fromArgb("#fff7f7f7"); }
    inline QColor BrandOnContainer() { return fromArgb("#ff7b2118"); }
    inline QColor BrandPressed() { return fromArgb("#ffA00005"); }

    // Button colors - using original MEGA red #D90007 for consistency
    inline QColor ButtonBrand() { return fromArgb("#ffD90007"); }
    inline QColor ButtonBrandHover() { return fromArgb("#ffC00006"); }
    inline QColor ButtonBrandPressed() { return fromArgb("#ffA00005"); }
    inline QColor ButtonDisabled() { return fromArgb("#ffe5e5e5"); }
    inline QColor ButtonError() { return fromArgb("#ffe31b57"); }
    inline QColor ButtonErrorHover() { return fromArgb("#ffc0104a"); }
    inline QColor ButtonErrorPressed() { return fromArgb("#ffa11045"); }
    inline QColor ButtonOutline() { return fromArgb("#ff04101e"); }
    inline QColor ButtonOutlineBackgroundHover() { return fromArgb("#0d000000"); }
    inline QColor ButtonOutlineHover() { return fromArgb("#ff39424e"); }
    inline QColor ButtonOutlinePressed() { return fromArgb("#ff535b65"); }
    inline QColor ButtonPrimary() { return fromArgb("#ff04101e"); }
    inline QColor ButtonPrimaryHover() { return fromArgb("#ff39424e"); }
    inline QColor ButtonPrimaryPressed() { return fromArgb("#ff535b65"); }
    inline QColor ButtonSecondary() { return fromArgb("#1a616366"); }
    inline QColor ButtonSecondaryHover() { return fromArgb("#33616366"); }
    inline QColor ButtonSecondaryPressed() { return fromArgb("#4d616366"); }

    // Interactive colors - using original MEGA red #D90007
    inline QColor ComponentsInteractive() { return fromArgb("#ffD90007"); }
    inline QColor FocusColor() { return fromArgb("#ffbdd9ff"); }

    // Icon colors - using original MEGA red #D90007
    inline QColor IconAccent() { return fromArgb("#ff04101e"); }
    inline QColor IconBrand() { return fromArgb("#ffD90007"); }
    inline QColor IconDisabled() { return fromArgb("#ffc1c2c4"); }
    inline QColor IconInverse() { return fromArgb("#fffafafa"); }
    inline QColor IconInverseAccent() { return fromArgb("#fffafafb"); }
    inline QColor IconInverseSecondary() { return fromArgb("#ffb0b1b3"); }
    inline QColor IconOnColor() { return fromArgb("#fffafafa"); }
    inline QColor IconOnColorDisabled() { return fromArgb("#ffa9abad"); }
    inline QColor IconPrimary() { return fromArgb("#ff303233"); }
    inline QColor IconSecondary() { return fromArgb("#ff616366"); }

    // Indicator colors
    inline QColor IndicatorBlue() { return fromArgb("#ff05baf1"); }
    inline QColor IndicatorGreen() { return fromArgb("#ff09bf5b"); }
    inline QColor IndicatorIndigo() { return fromArgb("#ff477ef7"); }
    inline QColor IndicatorMagenta() { return fromArgb("#ffe248c2"); }
    inline QColor IndicatorOrange() { return fromArgb("#fffb6514"); }
    inline QColor IndicatorPink() { return fromArgb("#fff63d6b"); }
    inline QColor IndicatorYellow() { return fromArgb("#fff7a308"); }

    // Link colors
    inline QColor LinkInverse() { return fromArgb("#ff69a3fb"); }
    inline QColor LinkPrimary() { return fromArgb("#ff2c5beb"); }
    inline QColor LinkVisited() { return fromArgb("#ff233783"); }

    // Neutral colors
    inline QColor NeutralContainerDefault() { return fromArgb("#1a616366"); }
    inline QColor NeutralContainerHover() { return fromArgb("#33616366"); }
    inline QColor NeutralContainerPressed() { return fromArgb("#4d616366"); }
    inline QColor NeutralDefault() { return fromArgb("#ff616366"); }
    inline QColor NeutralHover() { return fromArgb("#ff525457"); }
    inline QColor NeutralOnContainer() { return fromArgb("#ff616366"); }
    inline QColor NeutralOnGrey() { return fromArgb("#fff7f7f7"); }
    inline QColor NeutralPressed() { return fromArgb("#ff444547"); }

    // Notification colors
    inline QColor NotificationError() { return fromArgb("#ffffe4e8"); }
    inline QColor NotificationInfo() { return fromArgb("#ffdff4fe"); }
    inline QColor NotificationSuccess() { return fromArgb("#ffcffcdb"); }
    inline QColor NotificationWarning() { return fromArgb("#fffef4c6"); }

    // Selection colors
    inline QColor SelectionControl() { return fromArgb("#ff04101e"); }
    inline QColor SelectionControlAlt() { return fromArgb("#ff04101e"); }

    // Support colors
    inline QColor SupportError() { return fromArgb("#ffe31b57"); }
    inline QColor SupportInfo() { return fromArgb("#ff05baf1"); }
    inline QColor SupportSuccess() { return fromArgb("#ff009b48"); }
    inline QColor SupportWarning() { return fromArgb("#fff7a308"); }

    // Surface colors
    inline QColor Surface1() { return fromArgb("#fff7f7f7"); }
    inline QColor Surface2() { return fromArgb("#ffefeff0"); }
    inline QColor Surface3() { return fromArgb("#ffe4e4e5"); }
    inline QColor SurfaceInverseAccent() { return fromArgb("#ff39424e"); }
    inline QColor SurfaceTransparent() { return fromArgb("#b3000000"); }

    // Text colors
    inline QColor TextAccent() { return fromArgb("#ff04101e"); }
    inline QColor TextBrand() { return fromArgb("#ffD90007"); }
    inline QColor TextDisabled() { return fromArgb("#ffc1c2c4"); }
    inline QColor TextError() { return fromArgb("#ffe31b57"); }
    inline QColor TextInfo() { return fromArgb("#ff0078a4"); }
    inline QColor TextInverse() { return fromArgb("#fffafafa"); }
    inline QColor TextInverseAccent() { return fromArgb("#fffafafb"); }
    inline QColor TextInverseSecondary() { return fromArgb("#ffb0b1b3"); }
    inline QColor TextOnColor() { return fromArgb("#fffafafa"); }
    inline QColor TextOnColorDisabled() { return fromArgb("#ffa9abad"); }
    inline QColor TextPlaceholder() { return fromArgb("#ff616366"); }
    inline QColor TextPrimary() { return fromArgb("#ff303233"); }
    inline QColor TextSecondary() { return fromArgb("#ff616366"); }
    inline QColor TextSuccess() { return fromArgb("#ff007c3e"); }
    inline QColor TextWarning() { return fromArgb("#ffb55407"); }

    // Toast colors
    inline QColor ToastBackground() { return fromArgb("#ff494a4d"); }
}

namespace Dark {
    // Background colors
    inline QColor BackgroundBlur() { return fromArgb("#80000000"); }
    inline QColor BackgroundInverse() { return fromArgb("#ffefeff0"); }
    inline QColor PageBackground() { return fromArgb("#ff151616"); }

    // Border colors
    inline QColor BorderBrand() { return fromArgb("#fff23433"); }
    inline QColor BorderDisabled() { return fromArgb("#ff494a4d"); }
    inline QColor BorderStrong() { return fromArgb("#ff535455"); }
    inline QColor BorderStrongSelected() { return fromArgb("#fff4f4f5"); }
    inline QColor BorderSubtle() { return fromArgb("#ff252626"); }
    inline QColor BorderSubtleSelected() { return fromArgb("#fff4f4f5"); }

    // Brand colors
    inline QColor BrandContainerDefault() { return fromArgb("#4df23433"); }
    inline QColor BrandContainerHover() { return fromArgb("#66f23433"); }
    inline QColor BrandContainerPressed() { return fromArgb("#80f23433"); }
    inline QColor BrandDefault() { return fromArgb("#fff23433"); }
    inline QColor BrandHover() { return fromArgb("#fffb6361"); }
    inline QColor BrandOnBrand() { return fromArgb("#fff7f7f7"); }
    inline QColor BrandOnContainer() { return fromArgb("#fffcefef"); }
    inline QColor BrandPressed() { return fromArgb("#fffd9997"); }

    // Button colors
    inline QColor ButtonBrand() { return fromArgb("#fff23433"); }
    inline QColor ButtonBrandHover() { return fromArgb("#fffb6361"); }
    inline QColor ButtonBrandPressed() { return fromArgb("#fffd9997"); }
    inline QColor ButtonDisabled() { return fromArgb("#ff2c2d2d"); }
    inline QColor ButtonError() { return fromArgb("#fff63d6b"); }
    inline QColor ButtonErrorHover() { return fromArgb("#fffd6f90"); }
    inline QColor ButtonErrorPressed() { return fromArgb("#fffea3b5"); }
    inline QColor ButtonOutline() { return fromArgb("#fff4f4f5"); }
    inline QColor ButtonOutlineBackgroundHover() { return fromArgb("#0dffffff"); }
    inline QColor ButtonOutlineHover() { return fromArgb("#ffa3a6ad"); }
    inline QColor ButtonOutlinePressed() { return fromArgb("#ffbdc0c4"); }
    inline QColor ButtonPrimary() { return fromArgb("#fff4f4f5"); }
    inline QColor ButtonPrimaryHover() { return fromArgb("#ffa3a6ad"); }
    inline QColor ButtonPrimaryPressed() { return fromArgb("#ffbdc0c4"); }
    inline QColor ButtonSecondary() { return fromArgb("#33989a9c"); }
    inline QColor ButtonSecondaryHover() { return fromArgb("#4d989a9c"); }
    inline QColor ButtonSecondaryPressed() { return fromArgb("#66989a9c"); }

    // Interactive colors
    inline QColor ComponentsInteractive() { return fromArgb("#fff23433"); }
    inline QColor FocusColor() { return fromArgb("#ff2647d0"); }

    // Icon colors
    inline QColor IconAccent() { return fromArgb("#fffafafb"); }
    inline QColor IconBrand() { return fromArgb("#fff23433"); }
    inline QColor IconDisabled() { return fromArgb("#ff797c80"); }
    inline QColor IconInverse() { return fromArgb("#ff303233"); }
    inline QColor IconInverseAccent() { return fromArgb("#ff04101e"); }
    inline QColor IconInverseSecondary() { return fromArgb("#ff616366"); }
    inline QColor IconOnColor() { return fromArgb("#fffafafa"); }
    inline QColor IconOnColorDisabled() { return fromArgb("#ff919397"); }
    inline QColor IconPrimary() { return fromArgb("#fff3f4f4"); }
    inline QColor IconSecondary() { return fromArgb("#ffa9abad"); }

    // Indicator colors
    inline QColor IndicatorBlue() { return fromArgb("#ff31d0fe"); }
    inline QColor IndicatorGreen() { return fromArgb("#ff29dd74"); }
    inline QColor IndicatorIndigo() { return fromArgb("#ff69a3fb"); }
    inline QColor IndicatorMagenta() { return fromArgb("#fff4a8e3"); }
    inline QColor IndicatorOrange() { return fromArgb("#fffeb273"); }
    inline QColor IndicatorPink() { return fromArgb("#fffd6f90"); }
    inline QColor IndicatorYellow() { return fromArgb("#fffdc121"); }

    // Link colors
    inline QColor LinkInverse() { return fromArgb("#ff2c5beb"); }
    inline QColor LinkPrimary() { return fromArgb("#ff69a3fb"); }
    inline QColor LinkVisited() { return fromArgb("#ffd9e8ff"); }

    // Neutral colors
    inline QColor NeutralContainerDefault() { return fromArgb("#33989a9c"); }
    inline QColor NeutralContainerHover() { return fromArgb("#4d989a9c"); }
    inline QColor NeutralContainerPressed() { return fromArgb("#66989a9c"); }
    inline QColor NeutralDefault() { return fromArgb("#ffb0b1b3"); }
    inline QColor NeutralHover() { return fromArgb("#ffd7d8d9"); }
    inline QColor NeutralOnContainer() { return fromArgb("#ffb0b1b3"); }
    inline QColor NeutralOnGrey() { return fromArgb("#ff222324"); }
    inline QColor NeutralPressed() { return fromArgb("#ffefeff0"); }

    // Notification colors
    inline QColor NotificationError() { return fromArgb("#ff891240"); }
    inline QColor NotificationInfo() { return fromArgb("#ff085371"); }
    inline QColor NotificationSuccess() { return fromArgb("#ff01532b"); }
    inline QColor NotificationWarning() { return fromArgb("#ff94410b"); }

    // Selection colors
    inline QColor SelectionControl() { return fromArgb("#fff4f4f5"); }
    inline QColor SelectionControlAlt() { return fromArgb("#ff6e747d"); }

    // Support colors
    inline QColor SupportError() { return fromArgb("#fffd6f90"); }
    inline QColor SupportInfo() { return fromArgb("#ff0096c9"); }
    inline QColor SupportSuccess() { return fromArgb("#ff09bf5b"); }
    inline QColor SupportWarning() { return fromArgb("#fff7a308"); }

    // Surface colors
    inline QColor Surface1() { return fromArgb("#ff222324"); }
    inline QColor Surface2() { return fromArgb("#ff2a2b2c"); }
    inline QColor Surface3() { return fromArgb("#ff3a3b3d"); }
    inline QColor SurfaceInverseAccent() { return fromArgb("#ffbdc0c4"); }
    inline QColor SurfaceTransparent() { return fromArgb("#b3000000"); }

    // Text colors
    inline QColor TextAccent() { return fromArgb("#fffafafb"); }
    inline QColor TextBrand() { return fromArgb("#fff23433"); }
    inline QColor TextDisabled() { return fromArgb("#ff797c80"); }
    inline QColor TextError() { return fromArgb("#fffd6f90"); }
    inline QColor TextInfo() { return fromArgb("#ff05baf1"); }
    inline QColor TextInverse() { return fromArgb("#ff303233"); }
    inline QColor TextInverseAccent() { return fromArgb("#ff04101e"); }
    inline QColor TextInverseSecondary() { return fromArgb("#ff616366"); }
    inline QColor TextOnColor() { return fromArgb("#fffafafa"); }
    inline QColor TextOnColorDisabled() { return fromArgb("#ff919397"); }
    inline QColor TextPlaceholder() { return fromArgb("#ffc1c2c4"); }
    inline QColor TextPrimary() { return fromArgb("#fff3f4f4"); }
    inline QColor TextSecondary() { return fromArgb("#ffa9abad"); }
    inline QColor TextSuccess() { return fromArgb("#ff09bf5b"); }
    inline QColor TextWarning() { return fromArgb("#fff7a308"); }

    // Toast colors
    inline QColor ToastBackground() { return fromArgb("#ffc1c2c4"); }
}

// Commonly used direct color accessors (convenience)
// These are the most frequently used colors
namespace Common {
    // MEGA Brand Red - The iconic color
    inline QColor MegaRed() { return Light::BrandDefault(); }
    inline QColor MegaRedHover() { return Light::BrandHover(); }
    inline QColor MegaRedPressed() { return Light::BrandPressed(); }

    // For transfer status colors
    inline QColor TransferQueued() { return Light::IndicatorBlue(); }
    inline QColor TransferInProgress() { return Light::IndicatorGreen(); }
    inline QColor TransferCompleted() { return Light::SupportSuccess(); }
    inline QColor TransferFailed() { return Light::SupportError(); }
}

} // namespace DesignTokens
} // namespace MegaCustom

#endif // DESIGNTOKENS_H
