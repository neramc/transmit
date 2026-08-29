#include "core/services/ProfileService.h"

#include <QCoreApplication>

namespace transmit::core {
namespace {

CaptureRoot userRoot(PathTokenId token) {
    CaptureRoot root;
    root.token = token;
    root.domain = DomainId::UserData;
    root.recursive = true;
    return root;
}

CaptureSelection baseSelection() {
    CaptureSelection selection;
    selection.scope.excludePatterns = ScopeRule::defaultExcludes();
    return selection;
}

}  // namespace

CaptureProfile ProfileService::fullContinuity() {
    CaptureProfile profile;
    profile.id = QStringLiteral("full");
    profile.displayName = QCoreApplication::translate("Profile", "Everything that can travel");
    profile.description = QCoreApplication::translate(
        "Profile",
        "Your files, your application data and settings, your system preferences and the list of "
        "programs you have installed. This is the closest you can get to carrying on where you "
        "left off.");
    profile.sizeHint = QCoreApplication::translate("Profile", "Usually the largest option");

    profile.selection = baseSelection();
    profile.selection.domains = {
        static_cast<int>(DomainId::UserData), static_cast<int>(DomainId::AppState),
        static_cast<int>(DomainId::SystemSettings), static_cast<int>(DomainId::AppInventory)};
    for (const PathTokenId token :
         {PathTokenId::Documents, PathTokenId::Desktop, PathTokenId::Pictures, PathTokenId::Music,
          PathTokenId::Videos, PathTokenId::Downloads, PathTokenId::Templates}) {
        profile.selection.roots.push_back(userRoot(token));
    }

    // The catalog knows where a known application keeps its state, and can move
    // and repair it. An application it has never heard of gets none of that -
    // but its settings should still travel rather than being left behind
    // entirely, so the whole configuration tree comes too. Overlaps with the
    // recipe roots are removed by the scan.
    for (const PathTokenId token : {PathTokenId::AppConfig, PathTokenId::AppData,
                                    PathTokenId::AppState, PathTokenId::Fonts}) {
        CaptureRoot root;
        root.token = token;
        root.domain = DomainId::AppState;
        root.recursive = true;
        profile.selection.roots.push_back(root);
    }
    return profile;
}

QList<CaptureProfile> ProfileService::builtInProfiles() {
    QList<CaptureProfile> profiles;
    profiles.push_back(fullContinuity());

    {
        CaptureProfile documents;
        documents.id = QStringLiteral("documents");
        documents.displayName = QCoreApplication::translate("Profile", "Files only");
        documents.description = QCoreApplication::translate(
            "Profile",
            "Documents, pictures, music, video and downloads. No application data and no system "
            "settings, so it stays small and restores anywhere.");
        documents.sizeHint = QCoreApplication::translate("Profile", "Depends on your media");
        documents.selection = baseSelection();
        documents.selection.domains = {static_cast<int>(DomainId::UserData)};
        for (const PathTokenId token :
             {PathTokenId::Documents, PathTokenId::Desktop, PathTokenId::Pictures,
              PathTokenId::Music, PathTokenId::Videos, PathTokenId::Downloads}) {
            documents.selection.roots.push_back(userRoot(token));
        }
        profiles.push_back(documents);
    }

    {
        CaptureProfile developer;
        developer.id = QStringLiteral("developer");
        developer.displayName = QCoreApplication::translate("Profile", "Development setup");
        developer.description = QCoreApplication::translate(
            "Profile",
            "Shell configuration, editor and tool settings, SSH and Git configuration, and your "
            "documents. Build output and dependency folders are left behind because they are "
            "rebuilt anyway.");
        developer.sizeHint = QCoreApplication::translate("Profile", "Usually modest");
        developer.selection = baseSelection();
        developer.selection.domains = {static_cast<int>(DomainId::UserData),
                                       static_cast<int>(DomainId::AppState),
                                       static_cast<int>(DomainId::AppInventory)};
        developer.selection.roots.push_back(userRoot(PathTokenId::Documents));

        // The dotfiles a developer would notice missing within a minute.
        for (const QString& relative :
             {QStringLiteral(".bashrc"), QStringLiteral(".zshrc"), QStringLiteral(".profile"),
              QStringLiteral(".gitconfig"), QStringLiteral(".ssh"), QStringLiteral(".config/git"),
              QStringLiteral(".config/nvim"), QStringLiteral(".vimrc"),
              QStringLiteral(".tmux.conf")}) {
            CaptureRoot root;
            root.token = PathTokenId::Home;
            root.relative = relative;
            root.domain = DomainId::AppState;
            root.appId = QStringLiteral("shell");
            developer.selection.roots.push_back(root);
        }
        profiles.push_back(developer);
    }

    return profiles;
}

CaptureProfile ProfileService::profileById(const QString& id) {
    for (const CaptureProfile& profile : builtInProfiles()) {
        if (profile.id == id) {
            return profile;
        }
    }
    return fullContinuity();
}

}  // namespace transmit::core
