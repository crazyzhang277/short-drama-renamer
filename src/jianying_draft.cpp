#include "jianying_draft.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include "resource.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <system_error>

namespace jianying_draft {
namespace {

namespace fs = renamer::fs;
using Json = nlohmann::json;

constexpr wchar_t kTemplateTimelineId[] = L"FB12D1C8-4354-4120-B8B0-BF1A5AE3A979";

std::string ReadBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("无法读取文件: " + renamer::W2U8(path.wstring()));
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("无法写入文件: " + renamer::W2U8(path.wstring()));
}

bool ExtractEmbeddedTemplate(fs::path& root, std::wstring& error) {
    struct EmbeddedFile { UINT resource; const wchar_t* path; };
    static constexpr EmbeddedFile files[] = {
        {IDR_JY_PROJECT, L"Timelines\\project.json"},
        {IDR_JY_CONTENT, L"Timelines\\FB12D1C8-4354-4120-B8B0-BF1A5AE3A979\\draft_content.json"},
        {IDR_JY_ROOT_EDITING, L"attachment_editing.json"},
        {IDR_JY_ROOT_PC_COMMON, L"attachment_pc_common.json"},
        {IDR_JY_ROOT_ACTION_SCENE, L"common_attachment\\attachment_action_scene.json"},
        {IDR_JY_ROOT_ID_MAPPING, L"common_attachment\\attachment_id_mapping.json"},
        {IDR_JY_ROOT_PC_TIMELINE, L"common_attachment\\attachment_pc_timeline.json"},
        {IDR_JY_ROOT_SCRIPT_VIDEO, L"common_attachment\\attachment_script_video.json"},
        {IDR_JY_ROOT_COOPERATE_CREATE, L"common_attachment\\coperate_create.json"},
        {IDR_JY_TIMELINE_EDITING, L"Timelines\\FB12D1C8-4354-4120-B8B0-BF1A5AE3A979\\attachment_editing.json"},
        {IDR_JY_TIMELINE_PC_COMMON, L"Timelines\\FB12D1C8-4354-4120-B8B0-BF1A5AE3A979\\attachment_pc_common.json"},
        {IDR_JY_TIMELINE_ACTION_SCENE, L"Timelines\\FB12D1C8-4354-4120-B8B0-BF1A5AE3A979\\common_attachment\\attachment_action_scene.json"},
        {IDR_JY_TIMELINE_ID_MAPPING, L"Timelines\\FB12D1C8-4354-4120-B8B0-BF1A5AE3A979\\common_attachment\\attachment_id_mapping.json"},
        {IDR_JY_TIMELINE_PC_TIMELINE, L"Timelines\\FB12D1C8-4354-4120-B8B0-BF1A5AE3A979\\common_attachment\\attachment_pc_timeline.json"},
        {IDR_JY_TIMELINE_SCRIPT_VIDEO, L"Timelines\\FB12D1C8-4354-4120-B8B0-BF1A5AE3A979\\common_attachment\\attachment_script_video.json"},
        {IDR_JY_DRAFT_SETTINGS, L"draft_settings"},
        {IDR_JY_DRAFT_EXTRA, L"draft.extra"},
        {IDR_JY_KEY_VALUE, L"key_value.json"},
        {IDR_JY_PERFORMANCE, L"performance_opt_info.json"},
        {IDR_JY_AGENCY_CONFIG, L"draft_agency_config.json"},
        {IDR_JY_BIZ_CONFIG, L"draft_biz_config.json"}
    };

    const DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!needed) { error = L"找不到 Windows 的 LocalAppData 目录，无法准备内置参考草稿。"; return false; }
    std::wstring localData(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", localData.data(), needed);
    if (!written || written >= needed) { error = L"读取 LocalAppData 目录失败。"; return false; }
    localData.resize(written);
    root = fs::path(localData) / L"short-drama-renamer" / L"jianying-template-v1";

    try {
        for (const auto& file : files) {
            HRSRC found = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(file.resource), RT_RCDATA);
            if (!found) throw std::runtime_error("EXE 缺少内置模板资源。");
            const DWORD size = SizeofResource(GetModuleHandleW(nullptr), found);
            HGLOBAL loaded = LoadResource(GetModuleHandleW(nullptr), found);
            const char* data = static_cast<const char*>(loaded ? LockResource(loaded) : nullptr);
            if (size && !data) throw std::runtime_error("读取 EXE 内置模板资源失败。");
            std::string bytes;
            if (size) bytes.assign(data, size);
            const fs::path output = root / file.path;
            std::error_code ec;
            fs::create_directories(output.parent_path(), ec);
            if (ec) throw std::runtime_error("无法创建内置模板目录：" + renamer::W2U8(output.parent_path().wstring()));
            WriteBytes(output, bytes);
        }
    } catch (const std::exception& ex) {
        error = L"无法准备 EXE 内置剪映草稿模板：" + renamer::U82W(ex.what());
        return false;
    }
    return true;
}

void WriteJson(const fs::path& path, const Json& value) {
    WriteBytes(path, value.dump(2, ' ', false, Json::error_handler_t::replace));
}

Json ReadJson(const fs::path& path) {
    return Json::parse(ReadBytes(path));
}

std::string CanonicalGuid() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("无法生成草稿 ID。");
    wchar_t text[40]{};
    if (!StringFromGUID2(guid, text, 40)) throw std::runtime_error("无法格式化草稿 ID。");
    std::wstring value(text);
    value.erase(std::remove_if(value.begin(), value.end(), [](wchar_t c) {
        return c == L'{' || c == L'}';
    }), value.end());
    return renamer::W2U8(value);
}

std::string CompactGuid() {
    std::string id = CanonicalGuid();
    id.erase(std::remove(id.begin(), id.end(), '-'), id.end());
    std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return id;
}

std::uint64_t NowMicroseconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return std::towlower(c); });
    return value;
}

void SortClips(std::vector<fs::path>& files) {
    std::stable_sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        const int compare = renamer::NaturalCompare(a.filename().wstring(), b.filename().wstring());
        return compare == 0 ? a.wstring() < b.wstring() : compare < 0;
    });
}

bool CollectVideos(const fs::path& root, std::vector<fs::path>& files, std::wstring& error) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::none, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && renamer::IsVideoExt(renamer::LowerExt(it->path())))
            files.push_back(it->path());
        if (ec) break;
    }
    if (ec) { error = L"读取素材目录失败：" + fs::path(ec.message()).wstring(); return false; }
    return true;
}

void AddOrAppend(std::vector<Group>& groups, std::map<std::wstring, size_t>& index,
                 const std::wstring& name, std::vector<fs::path> files) {
    const std::wstring key = Lower(name);
    auto found = index.find(key);
    if (found == index.end()) {
        index.emplace(key, groups.size());
        groups.push_back({name, std::move(files)});
    } else {
        auto& target = groups[found->second].clips;
        target.insert(target.end(), files.begin(), files.end());
    }
}

bool ReadMediaInfo(const fs::path& path, std::uint64_t& durationUs,
                   std::uint32_t& width, std::uint32_t& height, std::wstring& error) {
    IMFSourceReader* reader = nullptr;
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
    if (FAILED(hr) || !reader) {
        error = L"Windows Media Foundation 无法读取视频：" + path.filename().wstring();
        return false;
    }
    PROPVARIANT duration;
    PropVariantInit(&duration);
    hr = reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration);
    if (FAILED(hr) || duration.vt != VT_UI8 || duration.uhVal.QuadPart == 0) {
        PropVariantClear(&duration);
        reader->Release();
        error = L"无法读取视频时长：" + path.filename().wstring();
        return false;
    }
    durationUs = duration.uhVal.QuadPart / 10;
    PropVariantClear(&duration);

    IMFMediaType* mediaType = nullptr;
    hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &mediaType);
    if (SUCCEEDED(hr) && mediaType) {
        hr = MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, &width, &height);
        mediaType->Release();
    }
    reader->Release();
    if (FAILED(hr) || width == 0 || height == 0) {
        error = L"无法读取视频画面尺寸：" + path.filename().wstring();
        return false;
    }
    return true;
}

struct MediaInfo {
    fs::path path;
    std::uint64_t durationUs = 0;
    std::uint64_t fileSize = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct SupportPrototype {
    std::string arrayName;
    Json value;
};

Json MakeTimeline(const Json& sample, const Group& group,
                  const std::vector<MediaInfo>& media) {
    if (media.empty() || media.size() != group.clips.size())
        throw std::runtime_error("时间线没有有效的视频素材。");
    if (!sample.contains("tracks") || !sample["tracks"].is_array())
        throw std::runtime_error("内置模板没有时间线轨道。");
    if (sample.contains("keyframes") && !sample["keyframes"].empty())
        throw std::runtime_error("内置模板包含生成器暂不支持的动画关键帧。");

    Json videoTrack;
    bool foundTrack = false;
    for (const auto& track : sample["tracks"]) {
        if (track.value("type", std::string()) == "video" &&
            track.contains("segments") && track["segments"].is_array() && !track["segments"].empty()) {
            videoTrack = track;
            foundTrack = true;
            break;
        }
    }
    if (!foundTrack) throw std::runtime_error("内置模板缺少含视频片段的视频轨道。");

    const Json segmentPrototype = videoTrack["segments"][0];
    const std::string prototypeMaterialId = segmentPrototype.value("material_id", std::string());
    if (!sample.contains("materials") || !sample["materials"].is_object() ||
        !sample["materials"].contains("videos") || !sample["materials"]["videos"].is_array())
        throw std::runtime_error("内置模板缺少视频素材信息。");
    Json videoPrototype;
    bool foundVideo = false;
    for (const auto& value : sample["materials"]["videos"]) {
        if (value.value("id", std::string()) == prototypeMaterialId) {
            videoPrototype = value;
            foundVideo = true;
            break;
        }
    }
    if (!foundVideo) throw std::runtime_error("内置模板的视频片段找不到对应素材。");

    std::vector<SupportPrototype> support;
    const Json refs = segmentPrototype.value("extra_material_refs", Json::array());
    for (const auto& refValue : refs) {
        const std::string ref = refValue.get<std::string>();
        bool matched = false;
        for (auto it = sample["materials"].begin(); it != sample["materials"].end() && !matched; ++it) {
            if (!it.value().is_array()) continue;
            for (const auto& item : it.value()) {
                if (item.value("id", std::string()) == ref) {
                    support.push_back({it.key(), item});
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) throw std::runtime_error("内置模板含无法解析的片段附属素材。");
    }

    Json doc = sample;
    Json materials = Json::object();
    for (auto it = sample["materials"].begin(); it != sample["materials"].end(); ++it)
        if (it.value().is_array()) materials[it.key()] = Json::array();
    if (!materials.contains("videos")) materials["videos"] = Json::array();

    Json segments = Json::array();
    std::uint64_t cursor = 0;
    for (size_t i = 0; i < media.size(); ++i) {
        const MediaInfo& info = media[i];
        if (!info.durationUs || info.durationUs > static_cast<std::uint64_t>(INT64_MAX) - cursor)
            throw std::runtime_error("视频时长超出剪映草稿支持范围。");

        const std::string videoId = CompactGuid();
        Json video = videoPrototype;
        video["id"] = videoId;
        video["material_id"] = videoId;
        video["path"] = renamer::W2U8(info.path.wstring());
        video["material_name"] = renamer::W2U8(info.path.filename().wstring());
        video["duration"] = info.durationUs;
        video["width"] = info.width;
        video["height"] = info.height;
        materials["videos"].push_back(std::move(video));

        Json extraRefs = Json::array();
        for (const auto& prototype : support) {
            const std::string id = CompactGuid();
            Json extra = prototype.value;
            extra["id"] = id;
            if (extra.contains("material_id")) extra["material_id"] = id;
            materials[prototype.arrayName].push_back(std::move(extra));
            extraRefs.push_back(id);
        }

        Json segment = segmentPrototype;
        segment["id"] = CompactGuid();
        segment["material_id"] = videoId;
        segment["extra_material_refs"] = std::move(extraRefs);
        segment["source_timerange"]["start"] = 0;
        segment["source_timerange"]["duration"] = info.durationUs;
        segment["target_timerange"]["start"] = cursor;
        segment["target_timerange"]["duration"] = info.durationUs;
        segments.push_back(std::move(segment));
        cursor += info.durationUs;
    }

    videoTrack["id"] = CompactGuid();
    videoTrack["type"] = "video";
    videoTrack["segments"] = std::move(segments);
    doc["id"] = CanonicalGuid();
    doc["name"] = renamer::W2U8(group.name);
    doc["duration"] = cursor;
    doc["tracks"] = Json::array({videoTrack});
    doc["materials"] = std::move(materials);
    return doc;
}

Json MakeMetaInfo(const std::string& draftId, const std::wstring& draftName,
                  const fs::path& draftPath, const fs::path& outputRoot,
                  std::uint64_t duration, std::uint64_t materialsSize, std::uint64_t now) {
    Json meta = {
        {"cloud_package_completed_time", ""},
        {"draft_cloud_capcut_purchase_info", ""},
        {"draft_cloud_last_action_download", false},
        {"draft_cloud_materials", Json::array()},
        {"draft_cloud_purchase_info", ""},
        {"draft_cloud_template_id", ""},
        {"draft_cloud_tutorial_info", ""},
        {"draft_cloud_videocut_purchase_info", ""},
        {"draft_cover", ""},
        {"draft_deeplink_url", ""},
        {"draft_enterprise_info", {{"draft_enterprise_extra", ""}, {"draft_enterprise_id", ""},
                                    {"draft_enterprise_name", ""}, {"enterprise_material", Json::array()}}},
        {"draft_fold_path", ""},
        {"draft_id", draftId},
        {"draft_is_ai_packaging_used", false},
        {"draft_is_ai_shorts", false},
        {"draft_is_ai_translate", false},
        {"draft_is_article_video_draft", false},
        {"draft_is_from_deeplink", "false"},
        {"draft_is_invisible", false},
        {"draft_materials", Json::array({
            {{"type", 0}, {"value", Json::array()}}, {{"type", 1}, {"value", Json::array()}},
            {{"type", 2}, {"value", Json::array()}}, {{"type", 3}, {"value", Json::array()}},
            {{"type", 6}, {"value", Json::array()}}, {{"type", 7}, {"value", Json::array()}},
            {{"type", 8}, {"value", Json::array()}}
        })},
        {"draft_materials_copied_info", Json::array()},
        {"draft_name", renamer::W2U8(draftName)},
        {"draft_new_version", ""},
        {"draft_removable_storage_device", renamer::W2U8(outputRoot.root_name().wstring())},
        {"draft_root_path", renamer::W2U8(outputRoot.wstring())},
        {"draft_segment_extra_info", Json::array()},
        {"draft_type", ""},
        {"tm_draft_cloud_completed", ""},
        {"tm_draft_cloud_modified", 0},
        {"tm_draft_removed", 0},
        {"tm_duration", duration},
        {"tm_draft_create", now},
        {"tm_draft_modified", now},
        {"draft_timeline_materials_size_", materialsSize}
    };
    meta["draft_fold_path"] = renamer::W2U8(draftPath.wstring());
    return meta;
}

void ReplaceAll(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

void CopyAuxiliary(const fs::path& source, const fs::path& destination,
                   const std::vector<std::pair<std::string, std::string>>& ids) {
    for (const wchar_t* filename : {L"attachment_editing.json", L"attachment_pc_common.json"}) {
        const fs::path input = source / filename;
        std::error_code ec;
        if (!fs::is_regular_file(input, ec)) continue;
        std::string text = ReadBytes(input);
        for (const auto& pair : ids) ReplaceAll(text, pair.first, pair.second);
        WriteBytes(destination / filename, text);
    }
    const fs::path common = source / L"common_attachment";
    std::error_code ec;
    if (!fs::is_directory(common, ec)) return;
    const fs::path target = destination / L"common_attachment";
    fs::create_directories(target);
    for (fs::directory_iterator it(common, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) { ec.clear(); continue; }
        std::string bytes = ReadBytes(it->path());
        for (const auto& pair : ids) ReplaceAll(bytes, pair.first, pair.second);
        WriteBytes(target / it->path().filename(), bytes);
    }
    if (ec) throw std::runtime_error("无法复制内置模板的公共附件。");
}

std::wstring SafeDraftName(std::wstring name) {
    for (wchar_t& c : name) {
        if (c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' ||
            c == L'\\' || c == L'|' || c == L'?' || c == L'*') c = L'_';
    }
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) name.pop_back();
    if (name.empty()) name = L"Jianying_Draft";
    if (name.size() > 100) name.resize(100);
    return name;
}

bool ReadTemplate(const fs::path& templateRoot,
                  Json& project, Json& content, fs::path& mainTimelineDir,
                  std::vector<std::string>& oldTimelineIds, std::string& oldProjectId,
                  std::wstring& error) {
    try {
        project = ReadJson(templateRoot / L"Timelines" / L"project.json");
        if (!project.contains("main_timeline_id") || !project["main_timeline_id"].is_string())
            throw std::runtime_error("内置模板缺少主时间线 ID。");
        oldProjectId = project.value("id", std::string());
        if (project.contains("timelines") && project["timelines"].is_array()) {
            for (const auto& timeline : project["timelines"])
                oldTimelineIds.push_back(timeline.value("id", std::string()));
        }
        const std::string mainId = project["main_timeline_id"].get<std::string>();
        mainTimelineDir = templateRoot / L"Timelines" / fs::u8path(mainId);
        fs::path contentPath = mainTimelineDir / L"draft_content.json";
        if (!fs::is_regular_file(contentPath)) contentPath = templateRoot / L"draft_content.json";
        content = ReadJson(contentPath);
        if (!content.contains("new_version") || !content.contains("id"))
            throw std::runtime_error("内置参考文件不是剪映时间线草稿内容。");
        if (oldTimelineIds.empty()) oldTimelineIds.push_back(mainId);
        return true;
    } catch (const std::exception& ex) {
        error = L"读取内置剪映模板失败：" + renamer::U82W(ex.what());
        return false;
    }
}

bool ValidateDraft(const fs::path& draft, size_t expectedGroups,
                   const std::vector<Group>& groups, size_t offset,
                   std::wstring& error) {
    try {
        Json project = ReadJson(draft / L"Timelines" / L"project.json");
        if (!project.contains("timelines") || !project["timelines"].is_array() ||
            project["timelines"].size() != expectedGroups)
            throw std::runtime_error("生成后的时间线数量不正确。");
        for (size_t i = 0; i < expectedGroups; ++i) {
            const std::string id = project["timelines"][i]["id"].get<std::string>();
            const Json content = ReadJson(draft / L"Timelines" / fs::u8path(id) / L"draft_content.json");
            if (!content.contains("tracks") || content["tracks"].size() != 1 ||
                content["tracks"][0].value("type", std::string()) != "video")
                throw std::runtime_error("生成的时间线没有保持单视频轨结构。");
            const Json& segments = content["tracks"][0]["segments"];
            const Group& group = groups[offset + i];
            if (segments.size() != group.clips.size())
                throw std::runtime_error("生成的片段数与预览不一致。");
            std::uint64_t cursor = 0;
            for (size_t j = 0; j < segments.size(); ++j) {
                const auto& target = segments[j]["target_timerange"];
                const std::uint64_t start = target.value("start", std::uint64_t(0));
                const std::uint64_t duration = target.value("duration", std::uint64_t(0));
                if (start != cursor || duration == 0)
                    throw std::runtime_error("生成的片段没有首尾连续排列。");
                cursor += duration;
                if (!fs::is_regular_file(group.clips[j]))
                    throw std::runtime_error("生成期间源视频被移动或删除。");
            }
            if (content.value("duration", std::uint64_t(0)) != cursor)
                throw std::runtime_error("时间线总时长与片段总时长不一致。");
        }
        if (fs::exists(draft / L"draft_cover.jpg"))
            throw std::runtime_error("输出中不应预制封面图。");
        return true;
    } catch (const std::exception& ex) {
        error = L"生成草稿自检失败：" + renamer::U82W(ex.what());
        return false;
    }
}

bool ScanOne(const fs::path& root, std::vector<fs::path>& files, std::wstring& error) {
    if (!CollectVideos(root, files, error)) return false;
    SortClips(files);
    return true;
}

} // namespace

bool ScanGroups(const fs::path& root, GroupMode mode, const std::wstring& filenameRegex,
                std::vector<Group>& groups, std::wstring& error) {
    groups.clear();
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) { error = L"请选择有效的素材目录。"; return false; }

    try {
        if (mode == GroupMode::AllFiles) {
            std::vector<fs::path> files;
            if (!ScanOne(root, files, error)) return false;
            if (!files.empty()) groups.push_back({root.filename().wstring(), std::move(files)});
        } else if (mode == GroupMode::Folders) {
            std::vector<fs::path> directories, directFiles;
            for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
                if (it->is_directory(ec)) directories.push_back(it->path());
                else if (it->is_regular_file(ec) && renamer::IsVideoExt(renamer::LowerExt(it->path())))
                    directFiles.push_back(it->path());
                if (ec) break;
            }
            if (ec) { error = L"读取素材目录失败。"; return false; }
            std::stable_sort(directories.begin(), directories.end(), [](const fs::path& a, const fs::path& b) {
                return renamer::NaturalCompare(a.filename().wstring(), b.filename().wstring()) < 0;
            });
            std::map<std::wstring, size_t> index;
            for (const auto& directory : directories) {
                std::vector<fs::path> files;
                if (!ScanOne(directory, files, error)) return false;
                if (!files.empty()) AddOrAppend(groups, index, directory.filename().wstring(), std::move(files));
            }
            if (!directFiles.empty()) {
                SortClips(directFiles);
                AddOrAppend(groups, index, root.filename().wstring(), std::move(directFiles));
            }
        } else {
            if (filenameRegex.empty()) { error = L"按文件名分组需要填写正则表达式。"; return false; }
            const std::wregex pattern(filenameRegex, std::regex_constants::ECMAScript | std::regex_constants::icase);
            std::vector<fs::path> files;
            if (!CollectVideos(root, files, error)) return false;
            std::map<std::wstring, size_t> index;
            for (const auto& file : files) {
                std::wsmatch match;
                const std::wstring stem = file.stem().wstring();
                if (!std::regex_match(stem, match, pattern) || match.size() < 2 || match[1].str().empty()) {
                    error = L"有文件不符合分组正则：" + file.filename().wstring();
                    groups.clear();
                    return false;
                }
                std::vector<fs::path> one{file};
                AddOrAppend(groups, index, match[1].str(), std::move(one));
            }
            for (auto& group : groups) SortClips(group.clips);
        }
        std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
            return renamer::NaturalCompare(a.name, b.name) < 0;
        });
        if (groups.empty()) { error = L"所选目录中没有找到可分组的视频文件。"; return false; }
        return true;
    } catch (const std::regex_error&) {
        error = L"文件名分组正则表达式无效。";
        groups.clear();
        return false;
    } catch (const std::exception& ex) {
        error = L"扫描素材失败：" + renamer::U82W(ex.what());
        groups.clear();
        return false;
    }
}

void NumberGroups(std::vector<Group>& groups, long start) {
    if (start <= 0) return;
    for (size_t i = 0; i < groups.size(); ++i)
        groups[i].name = std::to_wstring(start + static_cast<long>(i));
}

bool Generate(const BuildOptions& options, const std::vector<Group>& groups,
              const std::atomic<bool>& cancel, const Progress& progress,
              std::vector<fs::path>& outputs, std::wstring& error) {
    outputs.clear();
    if (groups.empty()) { error = L"请先预览并确认素材分组。"; return false; }
    if (options.timelinesPerDraft == 0 || options.timelinesPerDraft > 500) {
        error = L"每个草稿的时间线数量需在 1 到 500 之间。";
        return false;
    }
    std::error_code ec;
    if (!fs::is_directory(options.outputRoot, ec)) { error = L"请选择有效的草稿输出目录。"; return false; }
    fs::path templateRoot;
    if (!ExtractEmbeddedTemplate(templateRoot, error)) return false;

    const size_t batchCount = (groups.size() + options.timelinesPerDraft - 1) / options.timelinesPerDraft;
    const std::wstring baseName = SafeDraftName(options.sourceRoot.filename().wstring());
    std::vector<fs::path> finalPaths;
    for (size_t i = 0; i < batchCount; ++i) {
        wchar_t suffix[32]{};
        swprintf(suffix, 32, L"_Part_%02llu", static_cast<unsigned long long>(i + 1));
        fs::path final = options.outputRoot / (baseName + suffix);
        if (fs::exists(final, ec)) { error = L"目标草稿已存在，为防止覆盖已停止：\n" + final.wstring(); return false; }
        finalPaths.push_back(std::move(final));
    }

    Json templateProject, templateContent;
    fs::path mainTimelineDir;
    std::vector<std::string> oldTimelineIds;
    std::string oldProjectId;
    if (!ReadTemplate(templateRoot, templateProject, templateContent,
                      mainTimelineDir, oldTimelineIds, oldProjectId, error)) return false;

    size_t totalClips = 0;
    for (const auto& group : groups) {
        if (group.name.empty() || group.clips.empty()) { error = L"分组预览包含空名称或空组。"; return false; }
        totalClips += group.clips.size();
    }
    std::vector<std::vector<MediaInfo>> media(groups.size());
    size_t current = 0;
    try {
        for (size_t i = 0; i < groups.size(); ++i) {
            media[i].reserve(groups[i].clips.size());
            for (const fs::path& path : groups[i].clips) {
                if (cancel.load()) { error = L"生成已取消。"; return false; }
                MediaInfo info;
                info.path = path;
                if (!ReadMediaInfo(path, info.durationUs, info.width, info.height, error)) return false;
                info.fileSize = fs::file_size(path, ec);
                if (ec) { error = L"无法读取视频文件大小：" + path.filename().wstring(); return false; }
                media[i].push_back(std::move(info));
                ++current;
                if (progress) progress(current, totalClips, groups[i].name);
            }
        }

        const std::string buildId = CanonicalGuid();
        const fs::path staging = options.outputRoot / fs::u8path(".jianying-build-" + buildId + ".partial");
        if (!fs::create_directory(staging, ec) || ec) {
            error = L"无法创建草稿暂存目录：" + staging.wstring();
            return false;
        }

        std::vector<fs::path> published;
        try {
            std::vector<fs::path> stagedDrafts;
            stagedDrafts.reserve(batchCount);

            for (size_t batch = 0; batch < batchCount; ++batch) {
                if (cancel.load()) throw std::runtime_error("生成已取消。");
                const size_t offset = batch * options.timelinesPerDraft;
                const size_t count = std::min(options.timelinesPerDraft, groups.size() - offset);
                const fs::path staged = staging / finalPaths[batch].filename();
                fs::create_directories(staged / L"Timelines");
                const std::string projectId = CanonicalGuid();
                std::vector<std::string> ids;
                Json timelineList = Json::array();
                Json timelineNames = Json::array();
                Json mainDoc;
                std::uint64_t materialBytes = 0;
                std::uint64_t mainDuration = 0;
                std::vector<std::pair<std::string, std::string>> rootIdMap;
                if (!oldProjectId.empty()) rootIdMap.emplace_back(oldProjectId, projectId);

                for (size_t j = 0; j < count; ++j) {
                    const size_t groupIndex = offset + j;
                    const std::string timelineId = CanonicalGuid();
                    ids.push_back(timelineId);
                    timelineNames.push_back(renamer::W2U8(groups[groupIndex].name));
                    Json timeline = MakeTimeline(templateContent, groups[groupIndex], media[groupIndex]);
                    timeline["id"] = timelineId;
                    if (j == 0) { mainDoc = timeline; mainDuration = timeline["duration"].get<std::uint64_t>(); }
                    for (const auto& info : media[groupIndex]) materialBytes += info.fileSize;

                    const std::uint64_t now = NowMicroseconds();
                    timelineList.push_back({{"create_time", now}, {"id", timelineId},
                                            {"is_marked_delete", false},
                                            {"name", renamer::W2U8(groups[groupIndex].name)},
                                            {"update_time", now}});
                    const fs::path timelineDir = staged / L"Timelines" / fs::u8path(timelineId);
                    fs::create_directories(timelineDir);
                    std::vector<std::pair<std::string, std::string>> timelineIdMap;
                    if (!oldProjectId.empty()) timelineIdMap.emplace_back(oldProjectId, projectId);
                    for (const auto& oldId : oldTimelineIds)
                        if (!oldId.empty()) timelineIdMap.emplace_back(oldId, timelineId);
                    CopyAuxiliary(mainTimelineDir, timelineDir, timelineIdMap);
                    WriteJson(timelineDir / L"draft_content.json", timeline);
                    if (progress) progress(totalClips, totalClips,
                                           L"正在写入时间线 " + groups[groupIndex].name);
                }
                for (const auto& oldId : oldTimelineIds)
                    if (!oldId.empty()) rootIdMap.emplace_back(oldId, ids.front());
                CopyAuxiliary(templateRoot, staged, rootIdMap);
                for (const wchar_t* name : {L"draft_settings", L"draft.extra", L"key_value.json",
                                            L"performance_opt_info.json", L"draft_agency_config.json",
                                            L"draft_biz_config.json"}) {
                    const fs::path src = templateRoot / name;
                    if (fs::is_regular_file(src, ec)) fs::copy_file(src, staged / name, fs::copy_options::overwrite_existing);
                    ec.clear();
                }

                Json project = {
                    {"config", templateProject.value("config", Json::object())},
                    {"create_time", NowMicroseconds()},
                    {"id", projectId},
                    {"main_timeline_id", ids.front()},
                    {"timelines", timelineList},
                    {"update_time", NowMicroseconds()},
                    {"version", 0}
                };
                Json layout = {
                    {"activeTimeline", ids.front()},
                    {"dockItems", Json::array({{{"dockIndex", 0}, {"ratio", 1},
                                                {"timelineIds", ids}, {"timelineNames", timelineNames}}})},
                    {"layoutOrientation", 1}
                };
                WriteJson(staged / L"Timelines" / L"project.json", project);
                WriteJson(staged / L"timeline_layout.json", layout);
                WriteJson(staged / L"draft_content.json", mainDoc);
                const std::uint64_t now = NowMicroseconds();
                WriteJson(staged / L"draft_meta_info.json",
                          MakeMetaInfo(projectId, finalPaths[batch].filename().wstring(),
                                       finalPaths[batch], options.outputRoot,
                                       mainDuration, materialBytes, now));
                if (!ValidateDraft(staged, count, groups, offset, error))
                    throw std::runtime_error(renamer::W2U8(error));
                stagedDrafts.push_back(staged);
            }

            for (size_t i = 0; i < stagedDrafts.size(); ++i) {
                if (cancel.load()) throw std::runtime_error("生成已取消。");
                if (fs::exists(finalPaths[i], ec))
                    throw std::runtime_error("目标草稿在生成期间已出现，请移走后重试。");
                fs::rename(stagedDrafts[i], finalPaths[i], ec);
                if (ec) throw std::runtime_error("无法发布草稿目录，系统错误 " + std::to_string(ec.value()));
                published.push_back(finalPaths[i]);
            }
            fs::remove_all(staging, ec);
            outputs = std::move(published);
            return true;
        } catch (...) {
            std::error_code cleanup;
            fs::remove_all(staging, cleanup);
            for (const auto& path : published) fs::remove_all(path, cleanup);
            throw;
        }
    } catch (const std::exception& ex) {
        error = L"生成剪映草稿失败：" + renamer::U82W(ex.what());
        return false;
    }
}

bool SelfTest(std::wstring& error) {
    try {
        fs::path templateRoot;
        if (!ExtractEmbeddedTemplate(templateRoot, error)) return false;
        Json templateProject, templateContent;
        fs::path mainTimelineDir;
        std::vector<std::string> oldTimelineIds;
        std::string oldProjectId;
        if (!ReadTemplate(templateRoot, templateProject, templateContent, mainTimelineDir,
                          oldTimelineIds, oldProjectId, error)) return false;
        if (!templateContent.contains("tracks") || templateContent["tracks"].empty() ||
            !templateContent["materials"].contains("videos") ||
            templateContent["materials"]["videos"].empty()) {
            error = L"EXE 内置剪映模板缺少可用的视频轨道。";
            return false;
        }

        const fs::path root = fs::temp_directory_path() /
            fs::u8path("jy-group-test-" + std::to_string(GetCurrentProcessId()) + "-" + CompactGuid());
        fs::create_directories(root / L"Episode 10");
        fs::create_directories(root / L"Episode 2" / L"parts");
        for (const fs::path& path : {root / L"Episode 10" / L"Episode 10_Clip1.mp4",
                                     root / L"Episode 2" / L"parts" / L"Episode 2_Clip1.mp4",
                                     root / L"Episode 2" / L"Episode 2_Clip2.mp4"})
            WriteBytes(path, "test");

        std::vector<Group> groups;
        std::wstring message;
        if (!ScanGroups(root, GroupMode::Folders, L"", groups, message) || groups.size() != 2 ||
            groups[0].name != L"Episode 2" || groups[0].clips.size() != 2) {
            fs::remove_all(root);
            error = L"文件夹分组/自然排序自检失败。";
            return false;
        }
        const std::wstring pattern = L"^(Episode [0-9]+)_Clip[0-9]+$";
        if (!ScanGroups(root, GroupMode::FilenameRegex, pattern, groups, message) ||
            groups.size() != 2 || groups[0].clips.size() != 2) {
            fs::remove_all(root);
            error = L"文件名正则分组自检失败。";
            return false;
        }
        if (!ScanGroups(root, GroupMode::AllFiles, L"", groups, message) ||
            groups.size() != 1 || groups[0].clips.size() != 3) {
            fs::remove_all(root);
            error = L"单组模式自检失败。";
            return false;
        }
        std::vector<Group> named(2);
        named[0].name = L"Episode 2";
        named[1].name = L"Episode 10";
        NumberGroups(named, 9);
        if (named[0].name != L"9" || named[1].name != L"10") {
            fs::remove_all(root);
            error = L"时间线序号自检失败。";
            return false;
        }
        NumberGroups(named, 0);
        if (named[0].name != L"9") {
            fs::remove_all(root);
            error = L"时间线序号（关闭时不改名）自检失败。";
            return false;
        }
        fs::remove_all(root);
        return true;
    } catch (const std::exception& ex) {
        error = L"草稿生成器自检异常：" + renamer::U82W(ex.what());
        return false;
    }
}

} // namespace jianying_draft
