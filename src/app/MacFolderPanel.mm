#include "app/MacFolderPanel.h"

#import <AppKit/AppKit.h>

namespace reaction {

namespace {
NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }
}  // namespace

std::optional<std::filesystem::path> macChooseFolder(const std::filesystem::path& start, const std::string& message) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = NO;
        panel.canCreateDirectories = NO;
        panel.title = @"Covert Reaction";
        panel.message = ns(message);
        panel.prompt = @"Use This Folder";
        if (!start.empty())
            panel.directoryURL = [NSURL fileURLWithPath:ns(start.string()) isDirectory:YES];
        [NSApp activateIgnoringOtherApps:YES];
        if ([panel runModal] != NSModalResponseOK || panel.URLs.count == 0) return std::nullopt;
        const char* path = panel.URLs.firstObject.fileSystemRepresentation;
        if (!path) return std::nullopt;
        return std::filesystem::path(path);
    }
}

bool macAlert(const std::string& title, const std::string& text, const std::string& primary,
              const std::string& secondary) {
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = ns(title);
        alert.informativeText = ns(text);
        [alert addButtonWithTitle:ns(primary)];
        [alert addButtonWithTitle:ns(secondary)];
        [NSApp activateIgnoringOtherApps:YES];
        return [alert runModal] == NSAlertFirstButtonReturn;
    }
}

}  // namespace reaction
