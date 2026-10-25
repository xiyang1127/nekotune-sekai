#include "extension.h"
#include <nekotune/sdk/extension_plugin.h>
#include <QObject>

using namespace nekotune;

class SekaiPlugin final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::ExtensionContext &context, sdk::Done done) override {
        m_transport = std::make_unique<ext::NetworkTransport>();
        m_extension = std::make_unique<sekai::Extension>(context, *m_transport);
        m_extension->activate(std::move(done));
    }
    void deactivate() override {
        if (m_extension)
            m_extension->close();
        m_extension.reset();
        m_transport.reset();
    }

  private:
    std::unique_ptr<ext::NetworkTransport> m_transport;
    std::unique_ptr<sekai::Extension> m_extension;
};

#include "plugin.moc"
