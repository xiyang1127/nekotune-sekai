#pragma once
#include "download.h"
#include "provider.h"
#include <nekotune/sdk/extension_context.h>
#include <vector>

namespace sekai {
class Music;
/// Plugin logic, separate from the Qt plugin shell so tests can drive it with a fake context.
class Extension {
  public:
    Extension(nekotune::sdk::IExtensionContext &context, nekotune::ext::HttpTransport &transport);
    ~Extension();
    void activate(std::function<void(Result<void>)> done);
    void close();
    Provider &provider() { return *m_provider; }

  private:
    friend class Music;
    void reset();
    void service(const QString &id, nekotune::sdk::Handler handler);

    nekotune::sdk::IExtensionContext &m_context;
    nekotune::ext::HttpTransport &m_transport;
    std::unique_ptr<Provider> m_provider;
    // Replaced providers stay alive until close so late callbacks never reach freed objects.
    std::vector<std::unique_ptr<Provider>> m_retired;
    std::unique_ptr<DownloadPreparer> m_downloads;
    std::unique_ptr<Music> m_music;
    std::vector<nekotune::sdk::RegistrationPtr> m_registrations;
    bool m_closed = false;
};
} // namespace sekai
