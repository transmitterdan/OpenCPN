/***************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  PlugIn Manager Object
 * Author:   David Register
 *
 ***************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 **************************************************************************/

#include "config.h"

#include <algorithm>
#include <set>
#include <typeinfo>
#include <unordered_map>

#ifdef USE_LIBELF
#include <elf.h>
#include <libelf.h>
#include <gelf.h>
#endif

#if defined(__linux__) && !defined(__OCPN__ANDROID__)
#include <wordexp.h>
#endif

#ifndef WIN32
#include <cxxabi.h>
#endif

#include <wx/wx.h>
#include <wx/bitmap.h>
#include <wx/config.h>
#include <wx/dir.h>
#include <wx/event.h>
#include <wx/hashset.h>
#include <wx/filename.h>
#include <wx/string.h>
#include <wx/tokenzr.h>
#include <wx/window.h>

#include "plugin_loader.h"

#include "BasePlatform.h"
#include "ocpn_utils.h"
#include "logger.h"
#include "plugin_cache.h"
#include "PluginHandler.h"
#include "PluginPaths.h"
#include "safe_mode.h"

#ifdef __OCPN__ANDROID__
#include "androidUTIL.h"
#include <dlfcn.h>
#endif

extern wxConfigBase* pBaseConfig;
extern BasePlatform* g_BasePlatform;
extern wxWindow* gFrame;

const char* const LINUX_LOAD_PATH = "~/.local/lib:/usr/local/lib:/usr/lib";
const char* const FLATPAK_LOAD_PATH = "~/.var/app/org.opencpn.OpenCPN/lib";

static const std::vector<std::string> SYSTEM_PLUGINS = {
    "chartdownloader", "wmm", "dashboard", "grib"};
//          Helper and interface classes

PlugInContainer::PlugInContainer() {
  m_pplugin = NULL;
  m_bEnabled = false;
  m_bInitState = false;
  m_bToolboxPanel = false;
  m_bitmap = NULL;
  m_pluginStatus = PluginStatus::Unknown;
  m_api_version = 0;
}

SemanticVersion PlugInContainer::GetVersion() {
  if (m_ManagedMetadata.version.size()) {
    return SemanticVersion::parse(m_ManagedMetadata.version);
  }

  if (!m_pplugin) {
    return SemanticVersion(0, 0, 0);
  }
  auto plugin_117 = dynamic_cast<opencpn_plugin_117*>(m_pplugin);
  if (plugin_117) {
    return SemanticVersion(
        plugin_117->GetPlugInVersionMajor(),
        plugin_117->GetPlugInVersionMinor(),
        plugin_117->GetPlugInVersionPatch(), plugin_117->GetPlugInVersionPost(),
        plugin_117->GetPlugInVersionPre(), plugin_117->GetPlugInVersionBuild());
  } else {
    return SemanticVersion(m_pplugin->GetPlugInVersionMajor(),
                           m_pplugin->GetPlugInVersionMinor(),
                           -1);  // Don't print .patch. It's unknown
  }
}

//-----------------------------------------------------------------------------------------------------
//
//          Plugin Loader Implementation
//
//-----------------------------------------------------------------------------------------------------

/** Events sent to GUI */

/**
 *
 * For linux, set up LD_LIBRARY_PATH to the same value as the path used
 * to load plugins, assuring helper binaries can load libraries installed
 * in the same directory as the plugin.mac is handled the same way using
 * DYLD_LIBRARY_PATH. For windows, setup PATH in the likewise.
 */
static void setLoadPath() {
  using namespace std;

  auto const osSystemId = wxPlatformInfo::Get().GetOperatingSystemId();
  vector<string> dirs = PluginPaths::getInstance()->Libdirs();
  if (osSystemId & wxOS_UNIX_LINUX) {
    string path = ocpn::join(dirs, ':');
    wxString envPath;
    if (wxGetEnv("LD_LIBRARY_PATH", &envPath)) {
      path = path + ":" + envPath.ToStdString();
    }
    wxLogMessage("Using LD_LIBRARY_PATH: %s", path.c_str());
    wxSetEnv("LD_LIBRARY_PATH", path.c_str());
  } else if (osSystemId & wxOS_WINDOWS) {
    // On windows, Libdirs() and Bindirs() are the same.
    string path = ocpn::join(dirs, ';');
    wxString envPath;
    if (wxGetEnv("PATH", &envPath)) {
      path = path + ";" + envPath.ToStdString();
    }
    wxLogMessage("Using PATH: %s", path);
    wxSetEnv("PATH", path);
  } else if (osSystemId & wxOS_MAC) {
    string path = ocpn::join(dirs, ':');
    wxString envPath;
    if (wxGetEnv("DYLD_LIBRARY_PATH", &envPath)) {
      path = path + ":" + envPath.ToStdString();
    }
    wxLogMessage("Using DYLD_LIBRARY_PATH: %s", path.c_str());
    wxSetEnv("DYLD_LIBRARY_PATH", path.c_str());
  } else {
    wxString os_name = wxPlatformInfo::Get().GetPortIdName();
    if (os_name.Contains("wxQT")) {
      wxLogMessage("setLoadPath() using Android library path");
    } else
      wxLogWarning("SetLoadPath: Unsupported platform.");
  }
  if (osSystemId & wxOS_MAC || osSystemId & wxOS_UNIX_LINUX) {
    vector<string> dirs = PluginPaths::getInstance()->Bindirs();
    string path = ocpn::join(dirs, ':');
    wxString envPath;
    wxGetEnv("PATH", &envPath);
    path = path + ":" + envPath.ToStdString();
    wxLogMessage("Using PATH: %s", path);
    wxSetEnv("PATH", path);
  }
}

PluginLoader* PluginLoader::getInstance() {
  static PluginLoader* instance = 0;

  if (!instance) instance = new PluginLoader();
  return instance;
}

PluginLoader::PluginLoader(): m_default_plugin_icon(0) {}

bool PluginLoader::IsPlugInAvailable(wxString commonName) {
  for (unsigned int i = 0; i < plugin_array.GetCount(); i++) {
    PlugInContainer* pic = plugin_array[i];
    if (pic && pic->m_bEnabled && (pic->m_common_name == commonName))
      return true;
  }
  return false;
}

static void ProcessLateInit(PlugInContainer* pic) {
  if (pic->m_cap_flag & WANTS_LATE_INIT) {
    wxString msg("PlugInManager: Calling LateInit PlugIn: ");
    msg += pic->m_plugin_file;
    wxLogMessage(msg);

    opencpn_plugin_110* ppi =
        dynamic_cast<opencpn_plugin_110*>(pic->m_pplugin);
    if (ppi) ppi->LateInit();
  }
}

const wxBitmap* PluginLoader::GetPluginDefaultIcon() {
  if (!m_default_plugin_icon)
    m_default_plugin_icon = new wxBitmap(32, 32);
  return m_default_plugin_icon;
}

void PluginLoader::SetPluginDefaultIcon(const wxBitmap* bitmap) {
  delete m_default_plugin_icon;
  m_default_plugin_icon = bitmap;
}


bool PluginLoader::LoadAllPlugIns(bool load_enabled) {
  using namespace std;

  static const wxString sep = wxFileName::GetPathSeparator();
  vector<string> dirs = PluginPaths::getInstance()->Libdirs();
  wxLogMessage("PlugInManager: loading plugins from %s", ocpn::join(dirs, ';'));
  setLoadPath();
  bool any_dir_loaded = false;
  for (auto dir : dirs) {
    wxString wxdir(dir);
    wxLogMessage("Loading plugins from dir: %s", wxdir.mb_str().data());
    if (LoadPlugInDirectory(wxdir, load_enabled))
      any_dir_loaded = true;
  }

  // Read the default ocpn-plugins.xml, and update/merge the plugin array
  // This only needs to happen when the entire universe (enabled and disabled)
  // of plugins are loaded for management.
  if (!load_enabled) UpdateManagedPlugins();

  // Some additional actions needed after all plugins are loaded.
  evt_update_chart_types.notify();
  evt_plugin_loadall_finalize.notify();

  return any_dir_loaded;
}

bool PluginLoader::LoadPluginCandidate(wxString file_name, bool load_enabled) {

  wxString plugin_file = wxFileName(file_name).GetFullName();
  wxLogMessage("Checking plugin candidate: %s", file_name.mb_str().data());
  wxDateTime plugin_modification =
      wxFileName(file_name).GetModificationTime();

  // this gets called every time we switch to the plugins tab.
  // this allows plugins to be installed and enabled without restarting
  // opencpn. For this reason we must check that we didn't already load this
  // plugin
  bool loaded = false;
  PlugInContainer* loaded_pic = 0;
  for (unsigned int i = 0; i < plugin_array.GetCount(); i++) {
    PlugInContainer* pic_test = plugin_array[i];

    // Checking for dynamically updated plugins
    if (pic_test->m_plugin_filename == plugin_file) {
      // Do not re-load same-name plugins from different directories.  Certain
      // to crash...
      if (pic_test->m_plugin_file == file_name) {
        if (pic_test->m_plugin_modification != plugin_modification) {
          // modification times don't match, reload plugin
          plugin_array.Remove(pic_test);
          i--;

          DeactivatePlugIn(pic_test);
          pic_test->m_destroy_fn(pic_test->m_pplugin);

          delete pic_test;
        } else {
          loaded = true;
          loaded_pic = pic_test;
          break;
        }
      } else {
        loaded = true;
        loaded_pic = pic_test;
        break;
      }
    }
  }

  if (loaded) return true;

  //    Check the config file to see if this PlugIn is user-enabled

  const auto path = std::string("/PlugIns/") + plugin_file.ToStdString();
  ConfigVar<bool> enabled(path, "bEnabled", pBaseConfig);

  // only loading enabled plugins? check that it is enabled
  if (load_enabled && !enabled.get(true)) {
    wxLogMessage("Skipping not enabled candidate.");
    return true;
  }

  bool b_compat = CheckPluginCompatibility(file_name);

  if (!b_compat) {
    wxLogMessage("Incompatible plugin detected %s",
                 file_name.ToStdString().c_str());
    wxRenameFile(plugin_file, plugin_file + ".jail");
    wxLogMessage("Jailing: %s", plugin_file.ToStdString().c_str());
  }

  PlugInContainer* pic = NULL;
  if (b_compat) pic = LoadPlugIn(file_name);

  wxLog::FlushActive();

  if (pic) {
    if (pic->m_pplugin) {
      plugin_array.Add(pic);

      //    The common name is available without initialization and startup of
      //    the PlugIn
      pic->m_common_name = pic->m_pplugin->GetCommonName();
      pic->m_plugin_filename = plugin_file;
      pic->m_plugin_modification = plugin_modification;
      pic->m_bEnabled = enabled.get(false);

      if (safe_mode::get_mode()) {
        pic->m_bEnabled = false;
        enabled.set(false);
      }
#ifndef CLIAPP
      // The CLI has no graphics context, but plugins assumes there is.
      if (pic->m_bEnabled) {
        pic->m_cap_flag = pic->m_pplugin->Init();
        pic->m_bInitState = true;
        evt_load_plugin.notify(pic);
      }
#endif
      wxLog::FlushActive();

      std::string found_version;
      for (auto p : PluginHandler::getInstance()->getInstalled()) {
        if (p.name == pic->m_common_name.Lower()) {
          found_version = p.readonly ? "" : p.version;
          break;
        }
      }
      pic->m_version_str = found_version;
      pic->m_short_description = pic->m_pplugin->GetShortDescription();
      pic->m_long_description = pic->m_pplugin->GetLongDescription();
      pic->m_version_major = pic->m_pplugin->GetPlugInVersionMajor();
      pic->m_version_minor = pic->m_pplugin->GetPlugInVersionMinor();
      pic->m_bitmap = pic->m_pplugin->GetPlugInBitmap();

      wxBitmap* pbm = new wxBitmap(pic->m_bitmap->GetSubBitmap(wxRect(
          0, 0, pic->m_bitmap->GetWidth(), pic->m_bitmap->GetHeight())));
      pic->m_bitmap = pbm;

      if (!pic->m_bEnabled && pic->m_destroy_fn) {
        wxBitmap* pbm = new wxBitmap(pic->m_bitmap->GetSubBitmap(wxRect(
            0, 0, pic->m_bitmap->GetWidth(), pic->m_bitmap->GetHeight())));
        pic->m_bitmap = pbm;
        pic->m_destroy_fn(pic->m_pplugin);
        pic->m_destroy_fn = NULL;
        pic->m_pplugin = NULL;
        pic->m_bInitState = false;
        if (pic->m_library.IsLoaded()) pic->m_library.Unload();
      }

    } else  // not loaded
    {
      wxString msg;
      msg.Printf(
          "    PlugInManager: Unloading invalid PlugIn, API version %d ",
          pic->m_api_version);
      wxLogMessage(msg);

      pic->m_destroy_fn(pic->m_pplugin);

      delete pic;
    }
  }
  return true;
}

// Helper function: loads all plugins from a single directory
bool PluginLoader::LoadPlugInDirectory(const wxString& plugin_dir,
                                       bool load_enabled) {
  evt_load_directory.notify();
  m_plugin_location = plugin_dir;

  wxString msg("PlugInManager searching for PlugIns in location ");
  msg += m_plugin_location;
  wxLogMessage(msg);

#ifdef __WXMSW__
  wxString pispec = "*_pi.dll";
#elif defined(__WXOSX__)
  wxString pispec = "*_pi.dylib";
#else
  wxString pispec = "*_pi.so";
#endif

  if (!::wxDirExists(m_plugin_location)) {
    msg = m_plugin_location;
    msg.Prepend("   Directory ");
    msg.Append(" does not exist.");
    wxLogMessage(msg);
    return false;
  }

  if (!g_BasePlatform->isPlatformCapable(PLATFORM_CAP_PLUGINS)) return false;

  wxArrayString file_list;

  int get_flags = wxDIR_FILES | wxDIR_DIRS;
#ifdef __WXMSW__
#ifdef _DEBUG
  get_flags = wxDIR_FILES;
#endif
#endif

#ifdef __OCPN__ANDROID__
  get_flags = wxDIR_FILES;  // No subdirs, especially "/files" where PlugIns are
                            // initially placed in APK
#endif

  bool ret =
      false;  // return true if at least one new plugins gets loaded/unloaded
  wxDir::GetAllFiles(m_plugin_location, &file_list, pispec, get_flags);

  wxLogMessage("Found %d candidates", (int)file_list.GetCount());
  for (unsigned int i = 0; i < file_list.GetCount(); i++) {
    wxLog::FlushActive();

    wxString file_name = file_list[i];


    LoadPluginCandidate(file_name, load_enabled);
  }

  // Scrub the plugin array...
  // Here, looking for duplicates caused by new installation of a plugin
  // We want to remove the previous entry representing the uninstalled packaged
  // plugin metadata
  for (unsigned int i = 0; i < plugin_array.GetCount(); i++) {
    PlugInContainer* pic = plugin_array[i];
    for (unsigned int j = i + 1; j < plugin_array.GetCount(); j++) {
      PlugInContainer* pict = plugin_array[j];

      if (pic->m_common_name == pict->m_common_name) {
        if (pic->m_plugin_file.IsEmpty())
          plugin_array.Item(i)->m_pluginStatus =
              PluginStatus::PendingListRemoval;
        else
          plugin_array.Item(j)->m_pluginStatus =
              PluginStatus::PendingListRemoval;
      }
    }
  }

  //  Remove any list items marked
  size_t i = 0;
  while ((i >= 0) && (i < plugin_array.GetCount())) {
    PlugInContainer* pict = plugin_array.Item(i);
    if (pict->m_pluginStatus == PluginStatus::PendingListRemoval) {
      plugin_array.RemoveAt(i);
      i = 0;
    } else
      i++;
  }

  return ret;
}

bool PluginLoader::UpdatePlugIns() {
  bool bret = false;

  for (unsigned int i = 0; i < plugin_array.GetCount(); i++) {
    PlugInContainer* pic = plugin_array[i];

    // Try to confirm that the m_pplugin member points to a valid plugin
    // image...
    if (pic->m_pplugin) {
      opencpn_plugin* ppl = dynamic_cast<opencpn_plugin*>(pic->m_pplugin);
      if (!ppl) {
        pic->m_pplugin = NULL;
        pic->m_bInitState = false;
      }
    }

    // Installed and loaded?
    if (!pic->m_pplugin) {  // Needs a reload?
      if (pic->m_bEnabled) {
        PluginStatus stat = pic->m_pluginStatus;
        PlugInContainer* newpic = LoadPlugIn(pic->m_plugin_file, pic);
        if (newpic) {
          pic->m_pluginStatus = stat;
          pic->m_bEnabled = true;
        }
      } else
        continue;
    }

    if (pic->m_bEnabled && !pic->m_bInitState && pic->m_pplugin) {
      wxString msg("PlugInManager: Initializing PlugIn: ");
      msg += pic->m_plugin_file;
      wxLogMessage(msg);

      pic->m_cap_flag = pic->m_pplugin->Init();
      pic->m_pplugin->SetDefaults();
      pic->m_bInitState = true;
      ProcessLateInit(pic);
      pic->m_short_description = pic->m_pplugin->GetShortDescription();
      pic->m_long_description = pic->m_pplugin->GetLongDescription();
      pic->m_version_major = pic->m_pplugin->GetPlugInVersionMajor();
      pic->m_version_minor = pic->m_pplugin->GetPlugInVersionMinor();
      pic->m_bitmap = pic->m_pplugin->GetPlugInBitmap();
      bret = true;
    } else if (!pic->m_bEnabled && pic->m_bInitState) {
      // Save a local copy of the plugin icon before unloading
      wxBitmap* pbm = new wxBitmap(pic->m_bitmap->GetSubBitmap(
          wxRect(0, 0, pic->m_bitmap->GetWidth(), pic->m_bitmap->GetHeight())));
      pic->m_bitmap = pbm;

      bret = DeactivatePlugIn(pic);
      if (pic->m_pplugin) pic->m_destroy_fn(pic->m_pplugin);
      if (pic->m_library.IsLoaded()) pic->m_library.Unload();
      pic->m_pplugin = NULL;
      pic->m_bInitState = false;
    }
  }
  evt_update_chart_types.notify();
  return bret;
}

bool PluginLoader::DeactivatePlugIn(PlugInContainer* pic) {

  if (!pic) return false;
  if (pic->m_bInitState) {
    wxString msg("PlugInManager: Deactivating PlugIn: ");
    wxLogMessage(msg + pic->m_plugin_file);
    pic->m_bInitState = false;
    pic->m_pplugin->DeInit();
    // pic is doomed and will be deleted. Make a copy to handler which
    // can be used to look up items in toolbar etc.
    // FIXME: Correct solution is to use a shared_ptr instead, expanding
    // to a large patch covering many areas.
    auto pic_copy =
      static_cast<PlugInContainer*>(malloc(sizeof(PlugInContainer)));
    memcpy(pic_copy, pic, sizeof(PlugInContainer));
    evt_deactivate_plugin.notify(pic_copy);
  }
  return true;
}


/**
 * Return list of available, unique and compatible plugins from
 * configured XML catalog.
 */
// FIXME: Move to PluginHandler.
static std::vector<PluginMetadata> getCompatiblePlugins() {
  /** Compare two PluginMetadata objects, a named c++ requirement. */
  struct metadata_compare {
    bool operator()(const PluginMetadata& lhs,
                    const PluginMetadata& rhs) const {
      return lhs.key() < rhs.key();
    }
  };

  std::vector<PluginMetadata> returnArray;

  std::set<PluginMetadata, metadata_compare> unique_plugins;
  for (auto plugin : PluginHandler::getInstance()->getAvailable()) {
    unique_plugins.insert(plugin);
  }
  for (auto plugin : unique_plugins) {
    if (PluginHandler::isCompatible(plugin)) {
      returnArray.push_back(plugin);
    }
  }
  return returnArray;
}

bool PluginLoader::UnLoadPlugIn(size_t ix) {
  if (ix >= plugin_array.GetCount()) {
    wxLogWarning("Attempt to remove non-existing plugin %d", ix);
    return false;
  }
  PlugInContainer* pic = plugin_array[ix];
  if (!DeactivatePlugIn(pic)) {
    return false;
  }
  if (pic->m_pplugin) {
    pic->m_destroy_fn(pic->m_pplugin);
  }

  delete pic;  // This will unload the PlugIn via DTOR of pic->m_library
  plugin_array.RemoveAt(ix);
  return true;
}

void PluginLoader::UpdateManagedPlugins() {
  PlugInContainer* pict;
  // Clear the status (to "unmanaged") on all plugins
  for (size_t i = 0; i < plugin_array.GetCount(); i++) {
    pict = plugin_array.Item(i);
    plugin_array.Item(i)->m_pluginStatus = PluginStatus::Unmanaged;

    // Pre-mark the default "system" plugins
    auto r =
        std::find(SYSTEM_PLUGINS.begin(), SYSTEM_PLUGINS.end(),
                  plugin_array.Item(i)->m_common_name.Lower().ToStdString());
    if (r != SYSTEM_PLUGINS.end())
      plugin_array.Item(i)->m_pluginStatus = PluginStatus::System;
  }

  std::vector<PluginMetadata> available = getCompatiblePlugins();

  // Traverse the list again
  // Remove any inactive/uninstalled managed plugins that are no longer
  // available in the current catalog Usually due to reverting from Alpha/Beta
  // catalog back to master
  for (size_t i = 0; i < plugin_array.GetCount(); i++) {
    pict = plugin_array.Item(i);
    if (pict->m_ManagedMetadata.name
            .size()) {  // If metadata is good, must be a managed plugin
      bool bfound = false;
      for (auto plugin : available) {
        if (pict->m_common_name.IsSameAs(wxString(plugin.name.c_str()))) {
          bfound = true;
          break;
        }
      }
      if (!bfound) {
        if (!pict->m_pplugin) {  // Only remove inactive plugins
          plugin_array.Item(i)->m_pluginStatus =
              PluginStatus::PendingListRemoval;
        }
      }
    }
  }

  //  Remove any list items marked
  size_t i = 0;
  while ((i >= 0) && (i < plugin_array.GetCount())) {
    pict = plugin_array.Item(i);
    if (pict->m_pluginStatus == PluginStatus::PendingListRemoval) {
      plugin_array.RemoveAt(i);
      i = 0;
    } else
      i++;
  }

  for (size_t i = 0; i < plugin_array.GetCount(); i++) {
    pict = plugin_array.Item(i);
    int yyp = 4;
  }

  //  Now merge and update from the catalog
  for (auto plugin : available) {
    PlugInContainer* pic = NULL;
    // Search for an exact name match in the existing plugin array
    bool bfound = false;
    for (size_t i = 0; i < plugin_array.GetCount(); i++) {
      pic = plugin_array.Item(i);
      if (plugin_array.Item(i)->m_common_name.IsSameAs(
              wxString(plugin.name.c_str()))) {
        bfound = true;
        break;
      }
    }

    //  No match found, so add a container, and populate it
    if (!bfound) {
      PlugInContainer* new_pic = new PlugInContainer;
      new_pic->m_common_name = wxString(plugin.name.c_str());
      new_pic->m_pluginStatus = PluginStatus::ManagedInstallAvailable;
      new_pic->m_ManagedMetadata = plugin;
      new_pic->m_version_major = 0;
      new_pic->m_version_minor = 0;

      // In safe mode, check to see if the plugin appears to be installed
      // If so, set the status to "ManagedInstalledCurrentVersion", thus
      // enabling the "uninstall" button.
      if (safe_mode::get_mode()) {
        std::string installed;
        if (isRegularFile(PluginHandler::fileListPath(plugin.name).c_str())) {
          // Get the installed version from the manifest
          std::string path = PluginHandler::versionPath(plugin.name);
          if (path != "" && wxFileName::IsFileReadable(path)) {
            std::ifstream stream;
            stream.open(path, std::ifstream::in);
            stream >> installed;
          }
        }
        if (!installed.empty())
          new_pic->m_pluginStatus =
              PluginStatus::ManagedInstalledCurrentVersion;
        else
          new_pic->m_pluginStatus = PluginStatus::Unknown;
      }

      plugin_array.Add(new_pic);

    }
    // Match found, so merge the info and determine the plugin status
    else {
      // If the managed plugin is installed, the fileList (manifest) will be
      // present
      if (isRegularFile(PluginHandler::fileListPath(plugin.name).c_str())) {
        // Get the installed version from the manifest
        std::string installed;
        std::string path = PluginHandler::versionPath(plugin.name);
        if (path != "" && wxFileName::IsFileReadable(path)) {
          std::ifstream stream;
          stream.open(path, std::ifstream::in);
          stream >> installed;
        }
        pic->m_InstalledManagedVersion = installed;
        auto installedVersion = SemanticVersion::parse(installed);

        // Compare to the version reported in metadata
        auto metaVersion = SemanticVersion::parse(plugin.version);
        if (installedVersion < metaVersion)
          pic->m_pluginStatus = PluginStatus::ManagedInstalledUpdateAvailable;
        else if (installedVersion == metaVersion)
          pic->m_pluginStatus = PluginStatus::ManagedInstalledCurrentVersion;
        else if (installedVersion > metaVersion)
          pic->m_pluginStatus =
              PluginStatus::ManagedInstalledDowngradeAvailable;

        pic->m_ManagedMetadata = plugin;
      }

      // If the new plugin is not installed....
      else {
        // If the plugin is actually loaded, but the new plugin is known not to
        // be installed,
        //  then there must be a legacy plugin loaded.
        //  and the new status must be "PluginStatus::LegacyUpdateAvailable"
        if (pic->m_api_version) {
          pic->m_pluginStatus = PluginStatus::LegacyUpdateAvailable;
          pic->m_ManagedMetadata = plugin;
        }
        // Otherwise, this is an uninstalled managed plugin.
        else {
          pic->m_pluginStatus = PluginStatus::ManagedInstallAvailable;
        }
      }
    }
  }

  // Sort the list

  // Detach and hold the uninstalled, managed plugins
  std::map<std::string, PlugInContainer*> sortmap;
  for (unsigned int i = 0; i < plugin_array.GetCount(); i++) {
    PlugInContainer* pic = plugin_array[i];
    if (pic->m_pluginStatus == PluginStatus::ManagedInstallAvailable) {
      plugin_array.Remove(pic);

      // Sort by name, lower cased.
      std::string name = pic->m_ManagedMetadata.name;
      std::transform(name.begin(), name.end(), name.begin(), ::tolower);
      sortmap[name] = pic;
      i = 0;  // Restart the list
    }
  }

  // Add the detached plugins back at the top of the list.
  //  Later, the list will be populated in reverse order...Why??
  for (std::map<std::string, PlugInContainer*>::iterator i = sortmap.begin();
       i != sortmap.end(); i++) {
    PlugInContainer* pic = i->second;
    plugin_array.Insert(pic, 0);
  }
  evt_pluglist_change.notify();
}

bool PluginLoader::UnLoadAllPlugIns() {
  for (size_t i = 0; i < plugin_array.GetCount(); i++) {
    if (!UnLoadPlugIn(i)) {
      return false;
    }
  }
  return true;
}

bool PluginLoader::DeactivateAllPlugIns() {
  for (unsigned int i = 0; i < plugin_array.GetCount(); i++) {
    PlugInContainer* pic = plugin_array[i];
    if (pic && pic->m_bEnabled && pic->m_bInitState) DeactivatePlugIn(pic);
  }
  return true;
}

#ifdef __WXMSW__
/*Convert Virtual Address to File Offset */
DWORD Rva2Offset(DWORD rva, PIMAGE_SECTION_HEADER psh, PIMAGE_NT_HEADERS pnt) {
  size_t i = 0;
  PIMAGE_SECTION_HEADER pSeh;
  if (rva == 0) {
    return (rva);
  }
  pSeh = psh;
  for (i = 0; i < pnt->FileHeader.NumberOfSections; i++) {
    if (rva >= pSeh->VirtualAddress &&
        rva < pSeh->VirtualAddress + pSeh->Misc.VirtualSize) {
      break;
    }
    pSeh++;
  }
  return (rva - pSeh->VirtualAddress + pSeh->PointerToRawData);
}
#endif

class ModuleInfo {
public:
  WX_DECLARE_HASH_SET(wxString, wxStringHash, wxStringEqual, DependencySet);
  WX_DECLARE_HASH_MAP(wxString, wxString, wxStringHash, wxStringEqual,
                      DependencyMap);

  uint64_t type_magic;
  DependencyMap dependencies;
};

#ifdef USE_LIBELF
bool ReadModuleInfoFromELF(const wxString& file,
                           const ModuleInfo::DependencySet& dependencies,
                           ModuleInfo& info) {
  static bool b_libelf_initialized = false;
  static bool b_libelf_usable = false;

  if (b_libelf_usable) {
    // Nothing to do.
  } else if (b_libelf_initialized) {
    return false;
  } else if (elf_version(EV_CURRENT) == EV_NONE) {
    b_libelf_initialized = true;
    b_libelf_usable = false;
    wxLogError("LibELF is outdated.");
    return false;
  } else {
    b_libelf_initialized = true;
    b_libelf_usable = true;
  }

  int file_handle;
  Elf* elf_handle = NULL;
  GElf_Ehdr elf_file_header;
  Elf_Scn* elf_section_handle = NULL;

  file_handle = open(file, O_RDONLY);
  if (file_handle == -1) {
    wxLogError("Could not open file \"%s\" for reading: %s", file,
               strerror(errno));
    goto FailureEpilogue;
  }

  elf_handle = elf_begin(file_handle, ELF_C_READ, NULL);
  if (elf_handle == NULL) {
    wxLogError("Could not get ELF structures from \"%s\".", file);
    goto FailureEpilogue;
  }

  if (gelf_getehdr(elf_handle, &elf_file_header) != &elf_file_header) {
    wxLogError("Could not get ELF file header from \"%s\".", file);
    goto FailureEpilogue;
  }

  switch (elf_file_header.e_type) {
    case ET_EXEC:
    case ET_DYN:
      break;
    default:
      wxLogError(wxString::Format(
          "Module \"%s\" is not an executable or shared library.", file));
      goto FailureEpilogue;
  }

  info.type_magic =
      (static_cast<uint64_t>(elf_file_header.e_ident[EI_CLASS])
       << 0) |  // ELF class (32/64).
      (static_cast<uint64_t>(elf_file_header.e_ident[EI_DATA])
       << 8) |  // Endianness.
      (static_cast<uint64_t>(elf_file_header.e_ident[EI_OSABI])
       << 16) |  // OS ABI (Linux, FreeBSD, etc.).
      (static_cast<uint64_t>(elf_file_header.e_ident[EI_ABIVERSION])
       << 24) |  // OS ABI version.
      (static_cast<uint64_t>(elf_file_header.e_machine)
       << 32) |  // Instruction set.
      0;

  while ((elf_section_handle = elf_nextscn(elf_handle, elf_section_handle)) !=
         NULL) {
    GElf_Shdr elf_section_header;
    Elf_Data* elf_section_data = NULL;
    size_t elf_section_entry_count = 0;

    if (gelf_getshdr(elf_section_handle, &elf_section_header) !=
        &elf_section_header) {
      wxLogError("Could not get ELF section header from \"%s\".", file);
      goto FailureEpilogue;
    } else if (elf_section_header.sh_type != SHT_DYNAMIC) {
      continue;
    }

    elf_section_data = elf_getdata(elf_section_handle, NULL);
    if (elf_section_data == NULL) {
      wxLogError("Could not get ELF section data from \"%s\".", file);
      goto FailureEpilogue;
    }

    if ((elf_section_data->d_size == 0) ||
        (elf_section_header.sh_entsize == 0)) {
      wxLogError("Got malformed ELF section metadata from \"%s\".", file);
      goto FailureEpilogue;
    }

    elf_section_entry_count =
        elf_section_data->d_size / elf_section_header.sh_entsize;
    for (size_t elf_section_entry_index = 0;
         elf_section_entry_index < elf_section_entry_count;
         ++elf_section_entry_index) {
      GElf_Dyn elf_dynamic_entry;
      const char* elf_dynamic_entry_name = NULL;
      if (gelf_getdyn(elf_section_data, elf_section_entry_index,
                      &elf_dynamic_entry) != &elf_dynamic_entry) {
        wxLogError("Could not get ELF dynamic_section entry from \"%s\".",
                   file);
        goto FailureEpilogue;
      } else if (elf_dynamic_entry.d_tag != DT_NEEDED) {
        continue;
      }
      elf_dynamic_entry_name = elf_strptr(
          elf_handle, elf_section_header.sh_link, elf_dynamic_entry.d_un.d_val);
      if (elf_dynamic_entry_name == NULL) {
        wxLogError(wxString::Format("Could not get %s %s from \"%s\".", "ELF",
                                    "string entry", file));
        goto FailureEpilogue;
      }
      wxString name_full(elf_dynamic_entry_name);
      wxString name_part(elf_dynamic_entry_name,
                         strcspn(elf_dynamic_entry_name, "-."));
      if (dependencies.find(name_part) != dependencies.end()) {
        info.dependencies.insert(
            ModuleInfo::DependencyMap::value_type(name_part, name_full));
      }
    }
  };

  goto SuccessEpilogue;

SuccessEpilogue:
  elf_end(elf_handle);
  close(file_handle);
  return true;

FailureEpilogue:
  if (elf_handle != NULL) elf_end(elf_handle);
  if (file_handle >= 0) close(file_handle);
  return false;
}
#endif  // USE_LIBELF

bool PluginLoader::CheckPluginCompatibility(wxString plugin_file) {
  bool b_compat = true;

#ifdef __WXMSW__
  char strver[22];  // Enough space even for very big integers...
  sprintf(strver, "%i%i", wxMAJOR_VERSION, wxMINOR_VERSION);
  LPCWSTR fNmae = plugin_file.wc_str();
  HANDLE handle = CreateFile(fNmae, GENERIC_READ, 0, 0, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, 0);
  DWORD byteread, size = GetFileSize(handle, NULL);
  PVOID virtualpointer = VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
  ReadFile(handle, virtualpointer, size, &byteread, NULL);
  CloseHandle(handle);
  // Get pointer to NT header
  PIMAGE_NT_HEADERS ntheaders =
      (PIMAGE_NT_HEADERS)(PCHAR(virtualpointer) +
                          PIMAGE_DOS_HEADER(virtualpointer)->e_lfanew);
  PIMAGE_SECTION_HEADER pSech =
      IMAGE_FIRST_SECTION(ntheaders);  // Pointer to first section header
  PIMAGE_IMPORT_DESCRIPTOR pImportDescriptor;  // Pointer to import descriptor
  if (ntheaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
          .Size !=
      0) /*if size of the table is 0 - Import Table does not exist */
  {
    pImportDescriptor =
        (PIMAGE_IMPORT_DESCRIPTOR)((DWORD_PTR)virtualpointer +
                                   Rva2Offset(
                                       ntheaders->OptionalHeader
                                           .DataDirectory
                                               [IMAGE_DIRECTORY_ENTRY_IMPORT]
                                           .VirtualAddress,
                                       pSech, ntheaders));
    LPSTR libname[256];
    size_t i = 0;
    // Walk until you reached an empty IMAGE_IMPORT_DESCRIPTOR
    while (pImportDescriptor->Name != 0) {
      // Get the name of each DLL
      libname[i] =
          (PCHAR)((DWORD_PTR)virtualpointer +
                  Rva2Offset(pImportDescriptor->Name, pSech, ntheaders));
      if (strstr(libname[i], "wx") != NULL) {
        if (strstr(libname[i], strver) == NULL) b_compat = false;
        break;
      }
      pImportDescriptor++;  // advance to next IMAGE_IMPORT_DESCRIPTOR
      i++;
    }
  } else {
    wxLogMessage(
        wxString::Format("No Import Table! in %s", plugin_file.c_str()));
  }
  if (virtualpointer) VirtualFree(virtualpointer, size, MEM_DECOMMIT);
#endif
#if defined(__WXGTK__) || defined(__WXQT__)
#if defined(USE_LIBELF)

  static bool b_own_info_queried = false;
  static bool b_own_info_usable = false;
  static ModuleInfo own_info;
  static ModuleInfo::DependencySet dependencies;

  if (!b_own_info_queried) {
    dependencies.insert("libwx_baseu");
    const wxApp& app = *wxTheApp;
    if (app.argc && !app.argv[0].IsEmpty()) {
      wxString app_path(app.argv[0]);
      b_own_info_usable =
          ReadModuleInfoFromELF(app_path, dependencies, own_info);
    } else {
      wxLogError("Cannot get own executable path.");
    }
    b_own_info_queried = true;
  }

  if (b_own_info_usable) {
    bool b_pi_info_usable = false;
    ModuleInfo pi_info;
    b_pi_info_usable =
        ReadModuleInfoFromELF(plugin_file, dependencies, pi_info);
    if (b_pi_info_usable) {
      b_compat = (pi_info.type_magic == own_info.type_magic);
      if (1 /*g_BasePlatform->isFlatpacked()*/) {  // Ignore specific difference in
                                               // OSABI field on flatpak builds
        if ((pi_info.type_magic ^ own_info.type_magic) == 0x00030000)
          b_compat = true;
      }
      if (!b_compat) {
        pi_info.dependencies.clear();
        wxLogError(
            wxString::Format("    Plugin \"%s\" is of another binary "
                             "flavor than the main module.",
                             plugin_file));
        wxLogDebug("host magic: %.8x, plugin magic: %.8x", own_info.type_magic,
                   pi_info.type_magic);
      }
      for (ModuleInfo::DependencyMap::const_iterator own_dependency =
               own_info.dependencies.begin();
           own_dependency != own_info.dependencies.end(); ++own_dependency) {
        ModuleInfo::DependencyMap::const_iterator pi_dependency =
            pi_info.dependencies.find(own_dependency->first);
        if ((pi_dependency != pi_info.dependencies.end()) &&
            (pi_dependency->second != own_dependency->second)) {
          b_compat = false;
          wxLogError(
              "    Plugin \"%s\" depends on library \"%s\", but the main "
              "module was built for \"%s\".",
              plugin_file, pi_dependency->second, own_dependency->second);
          break;
        }
      }
    } else {
      b_compat = false;
      wxLogMessage(
          wxString::Format("    Plugin \"%s\" could not be reliably "
                           "checked for compatibility.",
                           plugin_file));
    }
  } else {
    // Allow any plugin when own info is not available.
    b_compat = true;
  }

  wxLogMessage("Plugin is compatible by elf library scan: %s",
               b_compat ? "true" : "false");
  return b_compat;

#endif  // LIBELF

  //  But Android Plugins do not include the wxlib specification in their ELF
  //  file. So we assume Android Plugins are compatible....
#ifdef __OCPN__ANDROID__
  return true;
#endif

  // If libelf is not available, then we must use a simplistic file scan method.
  // This is easily fooled if the wxWidgets version in use is not exactly
  // recognized. File scan is 3x faster than the ELF scan method

  FILE* f = fopen(plugin_file, "r");
  char strver[26];  // Enough space even for very big integers...

  sprintf(strver,
#if defined(__WXGTK3__)
          "libwx_gtk3u_core-%i.%i"
#elif defined(__WXGTK20__)
          "libwx_gtk2u_core-%i.%i"
#elif defined(__WXQT__)
          "libwx_qtu_core-%i.%i"
#else
#error undefined plugin platform
#endif
          ,
          wxMAJOR_VERSION, wxMINOR_VERSION);
  b_compat = false;

  int pos = 0, len = strlen(strver), c;
  while ((c = fgetc(f)) != EOF) {
    if (c == strver[pos]) {
      if (++pos == len) {
        b_compat = true;
        break;
      }
    } else
      pos = 0;
  }
  fclose(f);
#endif  // __WXGTK__ or __WXQT__

  wxLogMessage("Plugin is compatible: %s", b_compat ? "true" : "false");
  return b_compat;
}

bool PluginLoader::CheckBlacklistedPlugin(opencpn_plugin* plugin) {
  int len = sizeof(PluginBlacklist) / sizeof(BlackListedPlugin);
  int major = plugin->GetPlugInVersionMajor();
  int minor = plugin->GetPlugInVersionMinor();

#ifdef __WXMSW__
  wxString name = wxString::FromAscii(typeid(*plugin).name());
  name.Replace("class ", wxEmptyString);
#else
  const std::type_info& ti = typeid(*plugin);
  int status;
  char* realname = abi::__cxa_demangle(ti.name(), 0, 0, &status);
  wxString name = wxString::FromAscii(realname);
  free(realname);
#endif  // __WXMSW__
  for (int i = 0; i < len; i++) {
    if ((PluginBlacklist[i].all_lower && name == PluginBlacklist[i].name &&
         PluginBlacklist[i].version_major >= major &&
         PluginBlacklist[i].version_minor >= minor) ||
        (!PluginBlacklist[i].all_lower && name == PluginBlacklist[i].name &&
         PluginBlacklist[i].version_major == major &&
         PluginBlacklist[i].version_minor == minor)) {
      wxString msg;
      wxString msg1;
      if (PluginBlacklist[i].hard) {
        msg = wxString::Format(
            _("PlugIn %s (%s), version %i.%i was detected.\n This version is "
              "known to be unstable and will not be loaded.\n Please update "
              "this PlugIn using the PlugIn manager master catalog."),
            PluginBlacklist[i].name.c_str(), plugin->GetCommonName().c_str(),
            major, minor),
        _("Blacklisted plugin detected...");
        wxLogMessage(
            "    PlugIn %s (%s), version %i.%i was detected. Hard "
            "blacklisted. Not loaded.",
            PluginBlacklist[i].name.c_str(), plugin->GetCommonName().c_str(),
            major, minor);
      } else {
        msg = wxString::Format(
            _("PlugIn %s (%s), version %i.%i was detected.\n This version is "
              "known to be unstable.\n Please update this PlugIn using the "
              "PlugIn manager master catalog."),
            PluginBlacklist[i].name.c_str(), plugin->GetCommonName().c_str(),
            major, minor),
        _("Blacklisted plugin detected...");
        wxLogMessage(
            "    PlugIn %s (%s), version %i.%i was detected. Soft "
            "blacklisted. Loaded.",
            PluginBlacklist[i].name.c_str(), plugin->GetCommonName().c_str(),
            major, minor);
      }
      evt_blacklisted_plugin.notify(msg.ToStdString());
      return PluginBlacklist[i].hard;
    }
  }
  return false;
}

PlugInContainer* PluginLoader::LoadPlugIn(wxString plugin_file) {
  PlugInContainer* pic = new PlugInContainer;
  if (!LoadPlugIn(plugin_file, pic)) {
    delete pic;
    return 0;
  } else
    return pic;
}

PlugInContainer* PluginLoader::LoadPlugIn(wxString plugin_file,
                                          PlugInContainer* pic) {
  wxString msg("PlugInManager: Loading PlugIn: ");
  msg += plugin_file;
  wxLogMessage(msg);

  pic->m_plugin_file = plugin_file;
  pic->m_pluginStatus =
      PluginStatus::Unmanaged;  // Status is updated later, if necessary

  // load the library

  if (pic->m_library.IsLoaded()) pic->m_library.Unload();

  if (!wxIsReadable(plugin_file)) {
    evt_unreadable_plugin.notify(plugin_file.ToStdString());
    return 0;
  }

  pic->m_library.Load(plugin_file);

  if (!pic->m_library.IsLoaded()) {
    //  Look in the Blacklist, try to match a filename, to give some kind of
    //  message extract the probable plugin name
    wxFileName fn(plugin_file);
    wxString prob_pi_name;
    wxString name = fn.GetName();
    prob_pi_name = name;

#ifdef __WXGTK__
    prob_pi_name = name.Mid(3);  // lop off "lib"
#endif
#ifdef __WXOSX__
    prob_pi_name = name.Mid(3);  // lop off "lib"
#endif

    int len = sizeof(PluginBlacklist) / sizeof(BlackListedPlugin);
    for (int i = 0; i < len; i++) {
      wxString candidate = PluginBlacklist[i].name.Lower();
      if (prob_pi_name.Lower().EndsWith(candidate)) {
        msg = wxString::Format(_("Incompatible plugin detected:\n%s\n\n"),
                               plugin_file.c_str());

        msg += wxString::Format(_("PlugIn [ %s ] version %i.%i"),
                                PluginBlacklist[i].name.c_str(),
                                PluginBlacklist[i].version_major,
                                PluginBlacklist[i].version_minor);
        break;
      }
    }

    evt_version_incompatible_plugin.notify(plugin_file.ToStdString());
    wxLogMessage(msg);
    wxLogMessage("Jailing: %s", plugin_file.ToStdString().c_str());
    wxRenameFile(plugin_file, plugin_file + ".jail");

    return NULL;
  }

  // load the factory symbols
  create_t* create_plugin = (create_t*)pic->m_library.GetSymbol("create_pi");
  if (NULL == create_plugin) {
    wxString msg("   PlugInManager: Cannot load symbol create_pi: ");
    msg += plugin_file;
    wxLogMessage(msg);
    return NULL;
  }

  destroy_t* destroy_plugin =
      (destroy_t*)pic->m_library.GetSymbol("destroy_pi");
  pic->m_destroy_fn = destroy_plugin;
  if (NULL == destroy_plugin) {
    wxString msg("   PlugInManager: Cannot load symbol destroy_pi: ");
    msg += plugin_file;
    wxLogMessage(msg);
    return NULL;
  }

  // create an instance of the plugin class
  opencpn_plugin* plug_in = create_plugin(this);

  int api_major = plug_in->GetAPIVersionMajor();
  int api_minor = plug_in->GetAPIVersionMinor();
  int api_ver = (api_major * 100) + api_minor;
  pic->m_api_version = api_ver;

  int pi_major = plug_in->GetPlugInVersionMajor();
  int pi_minor = plug_in->GetPlugInVersionMinor();
  SemanticVersion pi_ver(pi_major, pi_minor, -1);

  if (CheckBlacklistedPlugin(plug_in)) {
    wxString dmsg(
        wxString::Format("%s: %s", "Jailing due to Blacklist", plugin_file));
    wxRenameFile(plugin_file, plugin_file + ".jail.blacklist");
    wxLogMessage(dmsg);
    return NULL;
  }

  switch (api_ver) {
    case 105:
      pic->m_pplugin = dynamic_cast<opencpn_plugin*>(plug_in);
      break;

    case 106:
      pic->m_pplugin = dynamic_cast<opencpn_plugin_16*>(plug_in);
      break;

    case 107:
      pic->m_pplugin = dynamic_cast<opencpn_plugin_17*>(plug_in);
      break;

    case 108:
      pic->m_pplugin = dynamic_cast<opencpn_plugin_18*>(plug_in);
      break;

    case 109:
      pic->m_pplugin = dynamic_cast<opencpn_plugin_19*>(plug_in);
      break;

    case 110:
      pic->m_pplugin = dynamic_cast<opencpn_plugin_110*>(plug_in);
      break;

    case 111:
      pic->m_pplugin = dynamic_cast<opencpn_plugin_111*>(plug_in);
      break;

    case 112:
      pic->m_pplugin = dynamic_cast<opencpn_plugin_112*>(plug_in);
      break;

    case 113:
      pic->m_pplugin = dynamic_cast<opencpn_plugin_113*>(plug_in);
      break;

    case 114:
      pic->m_pplugin = dynamic_cast<opencpn_plugin_114*>(plug_in);
      break;
    case 115:
      pic->m_pplugin = dynamic_cast<opencpn_plugin_115*>(plug_in);
      break;

    case 116:
      pic->m_pplugin = dynamic_cast<opencpn_plugin_116*>(plug_in);
      break;

    case 117:
      pic->m_pplugin = dynamic_cast<opencpn_plugin_117*>(plug_in);
      do /* force a local scope */ {
        auto p = dynamic_cast<opencpn_plugin_117*>(plug_in);
        pi_ver =
            SemanticVersion(pi_major, pi_minor, p->GetPlugInVersionPatch(),
                            p->GetPlugInVersionPost(), p->GetPlugInVersionPre(),
                            p->GetPlugInVersionBuild());
      } while (false);
      break;

    default:
      break;
  }

  if (pic->m_pplugin) {
    INFO_LOG << "PlugInManager:  " << plugin_file;
    INFO_LOG << "        Plugin common name: "
             << pic->m_pplugin->GetCommonName();
    INFO_LOG << "        API Version detected: " << api_ver;
    INFO_LOG << "        PlugIn Version detected: " << pi_ver;
  } else {
    INFO_LOG << "    " <<  plugin_file << " cannot be loaded";
  }

  return pic;
}
