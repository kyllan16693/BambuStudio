#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "ReleaseNote.hpp"
#include "I18N.hpp"
#include "slic3r/Utils/AppImageUpdater.hpp"
#include "slic3r/Utils/Http.hpp"

#include <atomic>
#include <exception>
#include <thread>
#include <wx/progdlg.h>

namespace Slic3r::GUI {
namespace {
bool install_appimage(wxWindow *parent, const std::string &image, const AppImageUpdater::Release &release)
{
    wxProgressDialog progress(_L("Update Bambu Studio"), _L("Downloading and verifying the update…"),
                              100, parent, wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME);
    std::atomic<bool> cancel{false}, done{false};
    std::atomic<int> percent{0};
    std::exception_ptr error;
    std::thread worker([&] {
        try { AppImageUpdater::download_and_install(image, release, cancel, percent); }
        catch (...) { error = std::current_exception(); }
        done = true;
    });
    // The worker never accesses wx objects. Update pumps the modal event loop;
    // join before any captured state or the progress window can be destroyed.
    while (!done.load()) {
        if (!progress.Update(percent.load())) cancel = true;
        wxMilliSleep(50);
    }
    worker.join();
    progress.Update(100);
    if (error) {
        if (!cancel.load()) {
            try { std::rethrow_exception(error); }
            catch (const std::exception &e) {
                wxMessageBox(_L("The update could not be installed. Your current version is still available.") +
                             "\n\n" + wxString::FromUTF8(e.what()), _L("Update failed"), wxOK | wxICON_ERROR, parent);
            }
        }
        return false;
    }
    return true;
}
} // namespace

bool GUI_App::check_appimage_update(bool show_tips)
{
    const std::string repository = BAMBUSTUDIO_UPDATE_REPOSITORY;
    const std::string asset = BAMBUSTUDIO_UPDATE_ASSET;
    const std::string image = AppImageUpdater::current_appimage();
    if (repository.empty() || image.empty()) return false;
    if (m_appimage_check_in_progress.exchange(true)) return true;
    const bool include_prereleases = app_config->get("enable_beta_version_update") == "true";
    // No app/cloud authorization headers are needed for public release metadata.
    auto http = Http::get("https://api.github.com/repos/" + repository + "/releases?per_page=100", false);
    http.header("Accept", "application/vnd.github+json")
        .header("User-Agent", "BambuStudio-AppImage-Updater")
        .timeout_connect(TIMEOUT_CONNECT).timeout_max(TIMEOUT_RESPONSE)
        .on_complete([this, repository, asset, image, include_prereleases, show_tips](std::string body, unsigned) {
            try {
                auto release = AppImageUpdater::select_release(body, repository, asset, SLIC3R_VERSION, include_prereleases);
                CallAfter([this, release, image, repository, show_tips] {
                    m_appimage_check_in_progress = false;
                    if (!release) {
                        if (show_tips) no_new_version();
                        return;
                    }
                    if (!show_tips && app_config->get("app", "skip_version") == release->version) return;
                    version_info.version_str = release->version;
                    UpdateVersionDialog dialog(mainframe);
                    dialog.update_version_info(wxString::FromUTF8(release->notes), wxString::FromUTF8(release->version), true);
                    dialog.m_link_open_in_browser->SetURL("https://github.com/" + repository + "/releases");
                    if (show_tips) dialog.m_button_skip_version->Hide();
                    if (dialog.ShowModal() != wxID_YES) return;
                    if (!install_appimage(mainframe, image, *release)) return;
                    const int choice = wxMessageBox(
                        _L("The update is installed. Restart Bambu Studio now? You will be asked to save any unsaved changes.") +
                        "\n\n" + _L("The previous version is saved beside the AppImage with a .previous suffix."),
                        _L("Update ready"), wxYES_NO | wxICON_INFORMATION, mainframe);
                    if (choice == wxYES) {
                        m_appimage_restart_path = image;
                        // Normal close preserves project/preset save prompts. A veto
                        // leaves the running version usable and the update on disk.
                        mainframe->Close();
                    }
                });
            } catch (const std::exception &e) {
                const std::string message = e.what();
                CallAfter([this, show_tips, message] {
                    m_appimage_check_in_progress = false;
                    if (show_tips) wxMessageBox(wxString::FromUTF8(message), _L("Update check failed"), wxOK | wxICON_ERROR, mainframe);
                });
            }
        })
        .on_error([this, show_tips](std::string, std::string, unsigned) {
            CallAfter([this, show_tips] {
                m_appimage_check_in_progress = false;
                if (show_tips) wxMessageBox(_L("Could not check for updates. Please check your connection and try again."),
                                           _L("Update check failed"), wxOK | wxICON_ERROR, mainframe);
            });
        }).perform();
    return true;
}

void GUI_App::restart_updated_appimage()
{
    if (m_appimage_restart_path.empty()) return;
    // Pass paths as argv, never interpolate them into shell code. Wait for this
    // process to exit so the single-instance guard accepts the restarted app.
    const wxString pid = wxString::Format("%ld", wxGetProcessId());
    const wxString script = wxString::FromUTF8(AppImageUpdater::restart_script());
    const wxString image = wxString::FromUTF8(m_appimage_restart_path);
    const wxChar *arguments[] = {wxT("/bin/sh"), wxT("-c"), script.c_str(), wxT("bambustudio-restart"), pid.c_str(), image.c_str(), nullptr};
    if (wxExecute(arguments, wxEXEC_ASYNC | wxEXEC_MAKE_GROUP_LEADER) == 0)
        wxLogError("Could not schedule Bambu Studio restart; launch the updated AppImage manually.");
}

} // namespace Slic3r::GUI
