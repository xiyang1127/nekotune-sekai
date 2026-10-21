#include "fake_context.h"
#include <nekotune/sdk/extension_plugin.h>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <optional>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 3)
        return 1;
    qunsetenv("KUGOU_ACCOUNT_API_KEY");
    qunsetenv("KUGOU_ACCOUNT_API_KEY_FILE");
    QTemporaryDir data;
    if (!data.isValid())
        return 1;
    QFile file(application.arguments().at(2));
    if (!file.open(QIODevice::ReadOnly))
        return 1;
    const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
    FakeContext context;
    context.dataPath = data.path();
    context.extensionId = manifest.value("id").toString();
    context.configuration = manifest.value("config").toObject().toVariantMap();
    if (context.extensionId == "nekotune.kugou")
        context.configuration = {{"legacy_imported", true}, {"enabled", false}, {"worker_url", ""}};
    QPluginLoader loader(application.arguments().at(1));
    loader.setLoadHints(QLibrary::PreventUnloadHint);
    auto *plugin = qobject_cast<nekotune::sdk::IExtensionPlugin *>(loader.instance());
    if (!plugin) {
        qCritical().noquote() << loader.errorString();
        return 1;
    }
    for (int cycle = 0; cycle < 2; ++cycle) {
        std::optional<nekotune::Result<void>> activated;
        plugin->activate(context, [&](nekotune::Result<void> result) { activated = result; });
        if (!activated || !*activated || !context.call("status")) {
            qCritical() << "Activation or status failed on cycle" << cycle;
            plugin->deactivate();
            return 1;
        }
        plugin->deactivate();
        if (!context.services.isEmpty() || !context.commands.isEmpty()
            || !context.music.isEmpty() || !context.lyrics.isEmpty() || !context.listeners.empty()) {
            qCritical() << "Registrations remain after deactivation on cycle" << cycle;
            return 1;
        }
    }
    return 0;
}
