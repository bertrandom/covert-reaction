#include "app/MacFolderPanel.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace reaction {

namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }

std::optional<std::filesystem::path> run(const std::filesystem::path& start, const std::string& message,
                                         const std::vector<std::string>* extensions) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = extensions ? YES : NO;
        panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = NO;
        panel.canCreateDirectories = NO;
        panel.title = @"Covert Reaction";
        panel.message = ns(message);
        panel.prompt = extensions ? @"Choose" : @"Use This Folder";
        if (extensions) {
            // Folders, and apps: the one GOG's macOS installer installs holds the
            // game (the panel shows an app as one item, not as a folder).
            NSMutableArray<UTType*>* types = [NSMutableArray arrayWithObjects:UTTypeFolder, UTTypeApplicationBundle, nil];
            for (const auto& e : *extensions) {
                const std::string ext = !e.empty() && e[0] == '.' ? e.substr(1) : e;
                if (UTType* t = [UTType typeWithFilenameExtension:ns(ext)]) [types addObject:t];
            }
            panel.allowedContentTypes = types;
        }
        if (!start.empty())
            panel.directoryURL = [NSURL fileURLWithPath:ns(start.string()) isDirectory:YES];
        [NSApp activateIgnoringOtherApps:YES];
        if ([panel runModal] != NSModalResponseOK || panel.URLs.count == 0) return std::nullopt;
        const char* path = panel.URLs.firstObject.fileSystemRepresentation;
        if (!path) return std::nullopt;
        return std::filesystem::path(path);
    }
}

}  // namespace

std::optional<std::filesystem::path> macChooseFolder(const std::filesystem::path& start, const std::string& message) {
    return run(start, message, nullptr);
}

std::optional<std::filesystem::path> macChooseFileOrFolder(const std::filesystem::path& start,
                                                           const std::string& message,
                                                           const std::vector<std::string>& extensions) {
    return run(start, message, &extensions);
}

}  // namespace reaction
