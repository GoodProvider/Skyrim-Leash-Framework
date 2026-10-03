#include "DebugPage.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../PCH.h"
#include "../../include/SKSEMenuFramework.h"
#include "../Leash/LeashManager.h"
#include "../Leash/SceneGraph.h"
#include "DebugOverlay.h"
#include "MenuLayout.h"
#include "ModMenu.h"

namespace LeashFramework::UI::DebugPage {
    namespace {
        enum class DebugAnchorType : std::uint8_t { kRightHand, kLeftHand, kActorBone, kWorldPosition };
        enum class MeshSource : std::uint8_t { kLeashed, kHolder, kStandalone };

        struct ActorOption {
            std::uint32_t formID{};
            std::string label;
            float distance{};
        };

        struct SkeletonNode {
            std::string name;
            std::string displayName;
            std::vector<SkeletonNode> children;
            bool likelyCandidate{};
        };

        constexpr std::string_view kSMPBoneMarker = "hdtSSEPhysics_";  // Used to help locate non-vanilla bones. Idc
        constexpr std::array kDebugAnchorLabels{"Right hand", "Left hand", "Actor bone", "World position"};
        constexpr std::array kMeshOwnerLabels{"Leashed actor", "Leasher", "Standalone NIF"};
        constexpr std::array kClosedHandLabels{"None", "Right", "Left"};
        constexpr std::array kFollowerLabels{"Leashed actor", "Leasher"};

        std::vector<ActorOption> actorOptions;
        std::uint32_t selectedHolder{};
        std::uint32_t selectedLeashed{};
        std::uint32_t selectedArmor{};
        DebugAnchorType selectedAnchorType{DebugAnchorType::kRightHand};
        char selectedAttachmentBone[128]{};
        RE::NiPoint3 selectedWorldPosition{};
        std::uint32_t selectedWorldCellFormID{};
        bool actorsLoaded{};
        bool actorCollisionDebugEnabled{};
        DebugSettings debugSettings;
        std::string status;
        std::vector<SkeletonNode> skeletonDump;
        std::string skeletonDumpActor;
        char skeletonFilter[128]{};
        bool skeletonSkinnedOnly{true};
        constexpr std::uint32_t kNoPreset = UINT32_MAX;
        std::uint32_t selectedPreset{kNoPreset};
        char presetName[64]{};
        bool confirmPresetDelete{};

        template <std::size_t N>
        void CopyText(char (&a_destination)[N], std::string_view a_source) {
            const auto length = std::min(a_source.size(), N - 1);
            a_source.copy(a_destination, length);
            a_destination[length] = '\0';
        }

        [[nodiscard]] std::string DescribeActor(RE::Actor* a_actor) {
            if (!a_actor) {
                return Locale::Text("Unavailable");
            }

            const auto* name = a_actor->GetDisplayFullName();
            return std::format("{} [{:08X}]", name && name[0] != '\0' ? name : Locale::Text("Unnamed actor"), a_actor->GetFormID());
        }

        [[nodiscard]] std::string DescribeActor(std::uint32_t a_formID) {
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formID)) {
                return DescribeActor(actor);
            }
            return Locale::Format("Unavailable [{0:08X}]", a_formID);
        }

        [[nodiscard]] MeshSource GetMeshSource() {
            return debugSettings.standaloneRope ? MeshSource::kStandalone : debugSettings.holderOwnsLeash ? MeshSource::kHolder : MeshSource::kLeashed;
        }

        void RefreshActors() {
            actorOptions.clear();
            std::unordered_set<std::uint32_t> formIDs;
            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto addActor = [&](RE::Actor* a_actor) {
                if (a_actor && formIDs.insert(a_actor->GetFormID()).second) {
                    const auto distance = player ? player->GetDistance(a_actor) : 0.0F;
                    actorOptions.push_back({a_actor->GetFormID(), DescribeActor(a_actor), distance});
                }
            };

            addActor(player);
            if (auto* processLists = RE::ProcessLists::GetSingleton()) {
                processLists->ForEachHighActor([&](RE::Actor* a_actor) {
                    addActor(a_actor);
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }

            const auto playerFormID = player ? player->GetFormID() : 0;
            std::ranges::sort(actorOptions, [&](const ActorOption& a_left, const ActorOption& a_right) {
                if (a_left.formID == playerFormID) {
                    return a_right.formID != playerFormID;
                }
                if (a_right.formID == playerFormID) {
                    return false;
                }
                if (a_left.distance != a_right.distance) {
                    return a_left.distance < a_right.distance;
                }
                return a_left.label < a_right.label;
            });

            const auto contains = [](std::uint32_t a_formID) { return std::ranges::any_of(actorOptions, [&](const ActorOption& a_option) { return a_option.formID == a_formID; }); };
            if (!contains(selectedHolder)) {
                selectedHolder = playerFormID != 0 ? playerFormID : actorOptions.empty() ? 0 : actorOptions.front().formID;
            }
            if (!contains(selectedLeashed)) {
                const auto option = std::ranges::find_if(actorOptions, [](const ActorOption& a_actor) { return a_actor.formID != selectedHolder; });
                selectedLeashed = option != actorOptions.end() ? option->formID : 0;
            }
            actorsLoaded = true;
        }

        void RenderActorDropdown(const char* a_label, std::uint32_t& a_selectedFormID) {
            const auto selected = std::ranges::find_if(actorOptions, [&](const ActorOption& a_actor) { return a_actor.formID == a_selectedFormID; });
            const char* preview = selected != actorOptions.end() ? selected->label.c_str() : Locale::Text("Select actor");
            MenuLayout::Field(a_label, [&](const char* a_id) {
                bool changed{};
                if (ImGuiMCP::BeginCombo(a_id, preview)) {
                    for (const auto& actor : actorOptions) {
                        changed |= MenuLayout::ChoiceItem(actor.label.c_str(), actor.formID, a_selectedFormID);
                    }
                    ImGuiMCP::EndCombo();
                }
                return changed;
            });
        }

        [[nodiscard]] bool CapturePlayerWorldAnchor() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            if (!player || !cell) {
                return false;
            }

            selectedWorldPosition = player->GetPosition();
            selectedWorldCellFormID = cell->GetFormID();
            return true;
        }

        [[nodiscard]] bool ContainsSMPBone(RE::NiAVObject& a_object) {
            const char* name = a_object.name.c_str();
            if (name && std::string_view{name}.contains(kSMPBoneMarker)) {
                return true;
            }
            auto* node = a_object.AsNode();
            if (!node) {
                return false;
            }
            return std::ranges::any_of(node->GetChildren(), [](const auto& a_child) { return a_child && ContainsSMPBone(*a_child); });
        }

        void CollectVisibleBonePointers(RE::NiAVObject& a_object, RE::NiAVObject* a_nearestUnrenamedParent, const std::unordered_set<RE::NiAVObject*>& a_skinnedBones, bool a_requireSMPMarker,
            std::unordered_set<RE::NiAVObject*>& a_visibleBones, std::unordered_set<RE::NiAVObject*>& a_likelyCandidates) {
            auto* node = a_object.AsNode();
            if (!node) {
                return;
            }

            const char* name = a_object.name.c_str();
            const bool hasSMPMarker = name && std::string_view{name}.contains(kSMPBoneMarker);
            if (a_skinnedBones.contains(&a_object) && (!a_requireSMPMarker || hasSMPMarker)) {
                a_visibleBones.insert(&a_object);
                if (a_requireSMPMarker) {
                    a_likelyCandidates.insert(&a_object);
                    if (a_nearestUnrenamedParent) {
                        a_visibleBones.insert(a_nearestUnrenamedParent);
                    }
                }
            }

            auto* nearestUnrenamedParent = hasSMPMarker ? a_nearestUnrenamedParent : &a_object;
            for (const auto& child : node->GetChildren()) {
                if (child) {
                    CollectVisibleBonePointers(*child, nearestUnrenamedParent, a_skinnedBones, a_requireSMPMarker, a_visibleBones, a_likelyCandidates);
                }
            }
        }

        void CollectAllNodePointers(RE::NiAVObject& a_object, std::unordered_set<RE::NiAVObject*>& a_visibleBones) {
            auto* node = a_object.AsNode();
            if (!node) {
                return;
            }
            a_visibleBones.insert(&a_object);
            for (const auto& child : node->GetChildren()) {
                if (child) {
                    CollectAllNodePointers(*child, a_visibleBones);
                }
            }
        }

        [[nodiscard]] std::vector<SkeletonNode> CaptureVisibleSkeleton(RE::NiAVObject& a_object, const std::unordered_set<RE::NiAVObject*>& a_visibleBones, const std::unordered_set<RE::NiAVObject*>& a_likelyCandidates,
            std::size_t& a_nodeCount) {
            std::vector<SkeletonNode> children;
            if (auto* node = a_object.AsNode()) {
                for (const auto& child : node->GetChildren()) {
                    if (!child) {
                        continue;
                    }
                    auto visibleChildren = CaptureVisibleSkeleton(*child, a_visibleBones, a_likelyCandidates, a_nodeCount);
                    for (auto& visibleChild : visibleChildren) {
                        children.push_back(std::move(visibleChild));
                    }
                }
            }

            if (!a_visibleBones.contains(&a_object)) {
                return children;
            }

            const char* objectName = a_object.name.c_str();
            SkeletonNode result{.name = objectName ? objectName : "", .children = std::move(children), .likelyCandidate = a_likelyCandidates.contains(&a_object)};
            result.displayName = result.name.empty() ? Locale::Text("<No Name>") : result.name;
            if (result.name.contains(kSMPBoneMarker)) {
                if (const auto separator = result.name.find(' '); separator != std::string::npos && separator + 1 < result.name.size()) {
                    result.displayName.erase(0, separator + 1);
                }
            }
            result.likelyCandidate = result.likelyCandidate || std::ranges::any_of(result.children, [](const SkeletonNode& a_child) { return a_child.likelyCandidate; });
            ++a_nodeCount;
            return {std::move(result)};
        }

        [[nodiscard]] bool SkeletonNodeMatchesFilter(const SkeletonNode& a_node, std::string_view a_filter) {
            if (a_filter.empty() || a_node.displayName.contains(a_filter)) {
                return true;
            }
            return std::ranges::any_of(a_node.children, [&](const SkeletonNode& a_child) { return SkeletonNodeMatchesFilter(a_child, a_filter); });
        }

        void RenderSkeletonNode(const SkeletonNode& a_node, std::string_view a_filter) {
            if (!SkeletonNodeMatchesFilter(a_node, a_filter)) {
                return;
            }

            const std::string_view leashMatch{debugSettings.leashBoneMatch};
            const bool isParent = !a_node.name.empty() && a_node.name == debugSettings.parentBone;
            const bool isLeashMatch = !leashMatch.empty() && a_node.name.contains(leashMatch);
            const auto label = std::format("{} [NiNode]", a_node.displayName);

            ImGuiMCP::ImGuiTreeNodeFlags treeFlags = ImGuiMCP::ImGuiTreeNodeFlags_SpanAvailWidth;
            if (isParent || isLeashMatch) {
                treeFlags |= ImGuiMCP::ImGuiTreeNodeFlags_Selected;
            }
            if (a_node.children.empty()) {
                treeFlags |= ImGuiMCP::ImGuiTreeNodeFlags_Leaf | ImGuiMCP::ImGuiTreeNodeFlags_NoTreePushOnOpen;
            } else if (!a_filter.empty()) {
                ImGuiMCP::SetNextItemOpen(true, ImGuiMCP::ImGuiCond_Always);
            } else if (a_node.likelyCandidate || (!leashMatch.empty() && SkeletonNodeMatchesFilter(a_node, leashMatch))) {
                ImGuiMCP::SetNextItemOpen(true, ImGuiMCP::ImGuiCond_Once);
            }

            ImGuiMCP::PushID(&a_node);
            const bool isOpen = ImGuiMCP::TreeNodeEx("##SkeletonNode", treeFlags, "%s", label.c_str());
            if (!a_node.name.empty() && ImGuiMCP::IsItemClicked(ImGuiMCP::ImGuiMouseButton_Right)) {
                ImGuiMCP::SetClipboardText(a_node.name.c_str());
                status = Locale::Format("Copied '{0}' to the clipboard.", a_node.name);
            }
            if (!a_node.children.empty() && isOpen) {
                for (const auto& child : a_node.children) {
                    RenderSkeletonNode(child, a_filter);
                }
                ImGuiMCP::TreePop();
            }
            ImGuiMCP::PopID();
        }

        void CollectSkinnedBonePointers(RE::NiAVObject& a_root, RE::NiNode& a_npcNode, std::unordered_set<RE::NiAVObject*>& a_visibleBones,
            std::unordered_set<RE::NiAVObject*>& a_likelyCandidates) {
            std::unordered_set<RE::NiAVObject*> skinnedBones;
            RE::BSVisit::TraverseScenegraphGeometries(&a_root, [&](RE::BSGeometry* a_geometry) {
                const auto skin = a_geometry->GetGeometryRuntimeData().skinInstance;
                if (!skin || !skin->bones) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }
                for (std::uint32_t index = 0, count = skin->skinData ? skin->skinData->GetBoneCount() : skin->numMatrices; index < count; ++index) {
                    if (auto* bone = skin->bones[index]; bone && bone->AsNode()) {
                        skinnedBones.insert(bone);
                    }
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });

            const bool requireSMPMarker = !debugSettings.standaloneRope && ContainsSMPBone(a_npcNode);
            CollectVisibleBonePointers(a_npcNode, &a_npcNode, skinnedBones, requireSMPMarker, a_visibleBones, a_likelyCandidates);
        }

        void DumpSelectedSkeleton() {
            skeletonDump.clear();
            skeletonDumpActor.clear();
            const auto meshOwnerFormID = debugSettings.holderOwnsLeash ? selectedHolder : selectedLeashed;
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(meshOwnerFormID);
            if (!actor) {
                status = Locale::Text("Select an available physical leash owner before dumping the skeleton.");
                return;
            }

            auto* root = debugSettings.standaloneRope ? LeashManager::GetSingleton().GetMeshRoot(actor) : actor->Get3D(false);
            if (!root) {
                status = debugSettings.standaloneRope ? Locale::Text("Apply a standalone test leash before inspecting its rope skeleton.") :
                    Locale::Format("{0} has no loaded third-person skeleton.", DescribeActor(actor));
                return;
            }

            auto* npcObject = debugSettings.standaloneRope ? root : root->GetObjectByName(RE::BSFixedString("NPC"));
            auto* npcNode = npcObject ? npcObject->AsNode() : nullptr;
            if (!npcNode) {
                status = Locale::Format("{0} has no loaded NPC skeleton node.", DescribeActor(actor));
                return;
            }

            std::unordered_set<RE::NiAVObject*> visibleBones{npcNode};
            std::unordered_set<RE::NiAVObject*> likelyCandidates;
            if (skeletonSkinnedOnly) {
                CollectSkinnedBonePointers(*root, *npcNode, visibleBones, likelyCandidates);
            } else {
                CollectAllNodePointers(*npcNode, visibleBones);
            }

            skeletonDumpActor = debugSettings.standaloneRope ? Locale::Format("Standalone rope for {0}", DescribeActor(actor)) : DescribeActor(actor);
            std::size_t nodeCount{};
            skeletonDump = CaptureVisibleSkeleton(*npcNode, visibleBones, likelyCandidates, nodeCount);
            status = Locale::Format("Displayed {0} skeleton node(s) for {1}.", nodeCount, skeletonDumpActor);
        }

        void RenderSkeletonDumper() {
            MenuLayout::Panel("Skeleton inspector", "Inspect skinned bones on the physical leash owner. The configured parent and matching rope bones are highlighted.", [] {
                if (MenuLayout::Toggle("Skinned bones only", skeletonSkinnedOnly,
                        "Show only bones used by skinned geometry. When the skeleton has SMP physics bones, only those are shown. Turn off to show every node.") &&
                    !skeletonDump.empty()) {
                    DumpSelectedSkeleton();
                }
                if (skeletonDump.empty()) {
                    MenuLayout::Note("Capture a skeleton to browse its bones.");
                    return;
                }
                ImGuiMCP::TextWrapped("%s", Locale::Format("Snapshot: {0}", skeletonDumpActor).c_str());
                MenuLayout::Text("Find a bone", skeletonFilter, "Show matching bones and their ancestors. Clear the filter to see the complete snapshot.", "Filter bone names");
                MenuLayout::Note("Right-click a bone to copy its full name.");
                const std::string_view filter{skeletonFilter};
                if (!std::ranges::any_of(skeletonDump, [&](const SkeletonNode& a_node) { return SkeletonNodeMatchesFilter(a_node, filter); })) {
                    MenuLayout::Note("No bones match this filter.");
                    return;
                }
                const auto height = ImGuiMCP::GetTextLineHeightWithSpacing() * 18.0F;
                if (ImGuiMCP::BeginChild("SkeletonDump", {0.0F, height}, ImGuiMCP::ImGuiChildFlags_Border)) {
                    for (const auto& root : skeletonDump) {
                        RenderSkeletonNode(root, filter);
                    }
                }
                ImGuiMCP::EndChild();
            }, DumpSelectedSkeleton, "Capture skeleton", "Refresh the snapshot from the selected physical leash owner's loaded skeleton.");
        }

        std::string DescribeArmor(RE::TESObjectARMO* a_armor) {
            const auto* name = a_armor->GetName();
            return std::format("{} ({:08X})", name && name[0] != '\0' ? name : Locale::Text("Unnamed armor"), a_armor->GetFormID());
        }

        void EquipArmor(RE::Actor* a_actor, RE::TESObjectARMO* a_armor) {
            if (!a_actor) {
                status = Locale::Text("Select an available physical leash owner before equipping armor.");
            } else if (!a_armor) {
                status = Locale::Text("Select an available armor before equipping.");
            } else if (auto* equipManager = RE::ActorEquipManager::GetSingleton(); !equipManager) {
                status = Locale::Text("The actor equip manager is unavailable.");
            } else {
                const auto inventory = a_actor->GetInventoryCounts();
                const auto item = inventory.find(a_armor);
                if (item == inventory.end() || item->second <= 0) {
                    a_actor->AddObjectToContainer(a_armor, nullptr, 1, nullptr);
                }
                equipManager->EquipObject(a_actor, a_armor, nullptr, 1, nullptr, true, true);
                status = Locale::Format("Equipped {0} on {1}.", DescribeArmor(a_armor), DescribeActor(a_actor));
            }
        }

        void RenderPluginArmorDropdown() {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            const auto* plugin = dataHandler ? dataHandler->LookupModByName("Leash.esm") : nullptr;
            if (!plugin || plugin->GetCompileIndex() == 0xFF) {
                ImGuiMCP::TextUnformatted(Locale::Text("Leash.esm armor is unavailable."));
                return;
            }

            auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(selectedArmor);
            if (armor && !plugin->IsFormInMod(armor->GetFormID())) {
                armor = nullptr;
            }
            const auto preview = armor ? DescribeArmor(armor) : std::string{Locale::Text("Select armor")};
            ImGuiMCP::SetNextItemWidth(-1.0F);
            if (ImGuiMCP::BeginCombo("##PluginArmor", preview.c_str())) {
                std::vector<RE::TESObjectARMO*> armors;
                for (auto* candidate : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
                    if (candidate && plugin->IsFormInMod(candidate->GetFormID())) {
                        armors.push_back(candidate);
                    }
                }
                std::ranges::sort(armors, {}, DescribeArmor);
                for (auto* candidate : armors) {
                    const auto label = DescribeArmor(candidate);
                    if (MenuLayout::ChoiceItem(label.c_str(), candidate->GetFormID(), selectedArmor)) {
                        armor = candidate;
                    }
                }
                if (armors.empty()) {
                    ImGuiMCP::TextUnformatted(Locale::Text("No armor found in Leash.esm."));
                }
                ImGuiMCP::EndCombo();
            }
            const auto actorFormID = debugSettings.holderOwnsLeash ? selectedHolder : selectedLeashed;
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorFormID);
            ImGuiMCP::BeginDisabled(!armor || !actor);
            if (MenuLayout::Button("Equip selected armor")) {
                EquipArmor(actor, armor);
            }
            ImGuiMCP::EndDisabled();
        }

        void EquipArmorEntry(const ArmorEntry& entry) {
            const auto meshOwnerFormID = debugSettings.holderOwnsLeash ? selectedHolder : selectedLeashed;
            auto* meshOwner = RE::TESForm::LookupByID<RE::Actor>(meshOwnerFormID);
            std::string_view formIDText{entry.formID};
            if (const auto comment = formIDText.find('#'); comment != std::string_view::npos) {
                formIDText = formIDText.substr(0, comment);
            }
            const auto firstCharacter = formIDText.find_first_not_of(" \t\r\n");
            if (firstCharacter == std::string_view::npos) {
                formIDText = {};
            } else {
                const auto lastCharacter = formIDText.find_last_not_of(" \t\r\n");
                formIDText = formIDText.substr(firstCharacter, lastCharacter - firstCharacter + 1);
            }
            if (formIDText.starts_with("0x") || formIDText.starts_with("0X")) {
                formIDText.remove_prefix(2);
            }

            std::uint32_t localFormID{};
            const auto parseResult = std::from_chars(formIDText.data(), formIDText.data() + formIDText.size(), localFormID, 16);
            if (!meshOwner) {
                status = Locale::Text("Select an available physical leash owner before equipping armor.");
            } else if (entry.modName[0] == '\0' || formIDText.empty()) {
                status = Locale::Text("Enter a mod name and local FormID.");
            } else if (parseResult.ec != std::errc{} || parseResult.ptr != formIDText.data() + formIDText.size()) {
                status = Locale::Format("{0} is not a valid hexadecimal FormID.", entry.formID);
            } else if (auto* dataHandler = RE::TESDataHandler::GetSingleton(); !dataHandler) {
                status = Locale::Text("The game data handler is unavailable.");
            } else if (const auto* plugin = dataHandler->LookupModByName(entry.modName); !plugin || plugin->GetCompileIndex() == 0xFF) {
                status = Locale::Format("Plugin {0} is not loaded.", entry.modName);
            } else {
                const auto resolvedFormID = dataHandler->LookupFormID(localFormID, entry.modName);
                auto* form = RE::TESForm::LookupByID(resolvedFormID);
                if (!form) {
                    status = Locale::Format("Could not find {0}:{1:X}; resolved runtime FormID {2:08X}.", entry.modName, localFormID, resolvedFormID);
                } else if (!form->Is(RE::FormType::Armor)) {
                    status = Locale::Format("Found a {0} record at {1}:{2:X}; equip requires an ARMO record.", RE::FormTypeToString(form->GetFormType()), entry.modName, localFormID);
                } else {
                    EquipArmor(meshOwner, static_cast<RE::TESObjectARMO*>(form));
                }
            }
        }

        void RenderArmorEntries() {
            static bool editEntries{};
            MenuLayout::Toggle("Edit shortcuts", editEntries, "Use a plugin name and hexadecimal local FormID. Add # followed by a name to label a shortcut.");
            for (std::size_t index = 0; index < debugSettings.armorEntries.size(); ++index) {
                auto& entry = debugSettings.armorEntries[index];
                ImGuiMCP::PushID(static_cast<int>(index));
                const std::string_view formID{entry.formID};
                const auto comment = formID.find('#');
                const auto name = comment != std::string_view::npos ? formID.substr(comment + 1) : formID;
                const auto label = name.empty() ? Locale::Format("Shortcut {0}", index + 1) : std::string{name};
                if (MenuLayout::Heading(std::string_view{label}, "Equip", "Equip this shortcut on the selected physical leash owner.")) {
                    EquipArmorEntry(entry);
                }
                if (editEntries) {
                    MenuLayout::Text("Plugin", entry.modName);
                    MenuLayout::Text("Local FormID #Name", entry.formID,
                        "Hexadecimal FormID within the plugin, optionally followed by # and a display name.");
                } else {
                    const auto source = std::format("{} / {}", entry.modName, formID.substr(0, comment));
                    MenuLayout::NoteRaw(source.c_str());
                }
                if (index + 1 < debugSettings.armorEntries.size()) {
                    ImGuiMCP::Separator();
                }
                ImGuiMCP::PopID();
            }
        }

        void RenderActiveLeashLength(RE::Actor* a_leashed, float a_length, bool a_minimum, float a_width) {
            ImGuiMCP::SetNextItemWidth(a_width);
            ImGuiMCP::BeginDisabled(!a_leashed);
            if (ImGuiMCP::InputFloat(a_minimum ? "##MinLength" : "##MaxLength", &a_length, 1.0F, 10.0F, "%.1f", ImGuiMCP::ImGuiInputTextFlags_EnterReturnsTrue)) {
                auto& manager = LeashManager::GetSingleton();
                const bool updated = a_minimum ? manager.SetMinLength(a_leashed, a_length) : manager.SetMaxLength(a_leashed, a_length);
                if (updated) {
                    status = a_minimum ? Locale::Format("Minimum distance for {0} set to {1:.1f}.", DescribeActor(a_leashed), a_length)
                                             : Locale::Format("Maximum distance for {0} set to {1:.1f}.", DescribeActor(a_leashed), a_length);
                } else {
                    status = Locale::Text(a_minimum
                        ? "Could not update minimum distance. Minimum must be non-negative, maximum must be positive, and minimum cannot exceed maximum."
                        : "Could not update maximum distance. Minimum must be non-negative, maximum must be positive, and minimum cannot exceed maximum.");
                }
            }
            ImGuiMCP::EndDisabled();
            MenuLayout::Help(a_minimum ? "Release / arrival distance. Must be between zero and the current maximum."
                                      : "Maximum follow distance. Must be positive and at least the current minimum.");
        }

        constexpr auto kFollowerHelp = "Which actor is pulled along. Choose Leasher to make the holder follow the leashed actor, like walking a dog. Leaning stays on the leashed actor.";

        [[nodiscard]] float GetFollowerComboWidth() {
            float width{};
            for (const auto* label : kFollowerLabels) {
                ImGuiMCP::ImVec2 size;
                ImGuiMCP::CalcTextSize(&size, Locale::Text(label), nullptr, false, -1.0F);
                width = std::max(width, size.x);
            }
            return width + ImGuiMCP::GetFrameHeight() + ImGuiMCP::GetStyle()->FramePadding.x * 2.0F;
        }

        // a_index uses LeashSide values
        bool FollowerCombo(int& a_index) {
            std::array<const char*, kFollowerLabels.size()> labels;
            std::ranges::transform(kFollowerLabels, labels.begin(), [](const char* a_label) { return Locale::Text(a_label); });
            ImGuiMCP::SetNextItemWidth(GetFollowerComboWidth());
            return ImGuiMCP::Combo("##Follower", &a_index, labels.data(), static_cast<int>(labels.size()));
        }

        void RenderActiveLeashFollower(RE::Actor* a_leashed, const LeashDefinition& a_definition) {
            auto index = static_cast<int>(a_definition.follower);
            ImGuiMCP::BeginDisabled(!a_leashed || a_definition.holderFormID == 0);
            if (FollowerCombo(index)) {
                if (LeashManager::GetSingleton().SetFollower(a_leashed, index)) {
                    const auto follower = static_cast<LeashSide>(index);
                    status = Locale::Format("{0} now follows {1}.", DescribeActor(a_definition.GetFormID(follower)), DescribeActor(a_definition.GetFormID(Opposite(follower))));
                } else {
                    status = Locale::Text("Could not change the follower. World-position leashes always pull the leashed actor.");
                }
            }
            ImGuiMCP::EndDisabled();
            MenuLayout::Help(kFollowerHelp);
        }

        // a_details is already localized
        void RenderActiveLeashActor(std::uint32_t a_formID, const std::string& a_details = {}) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
            const auto* name = actor ? actor->GetDisplayFullName() : Locale::Text("Unavailable");
            ImGuiMCP::AlignTextToFramePadding();
            ImGuiMCP::TextUnformatted(name && name[0] != '\0' ? name : Locale::Text("Unnamed actor"));
            if (ImGuiMCP::IsItemHovered() && ImGuiMCP::BeginTooltip()) {
                ImGuiMCP::TextUnformatted(DescribeActor(a_formID).c_str());
                if (!a_details.empty()) {
                    ImGuiMCP::TextUnformatted(a_details.c_str());
                }
                ImGuiMCP::EndTooltip();
            }
        }

        void RenderActiveLeashes(const std::vector<LeashDefinition>& a_definitions) {
            if (a_definitions.empty()) {
                ImGuiMCP::TextUnformatted(Locale::Text("No actors are currently leashed."));
                return;
            }
            MenuLayout::Note("Press Enter to apply a typed distance; +/- buttons apply immediately. X removes only that leash.");

            const auto em = ImGuiMCP::GetFontSize();
            const auto& style = *ImGuiMCP::GetStyle();
            const auto stepWidth = 2.0F * (ImGuiMCP::GetFrameHeight() + style.ItemInnerSpacing.x);
            const auto inputWidth = (em * 9.0F - stepWidth) * 0.5F + stepWidth;
            const auto textWidth = [](const char* a_label) {
                ImGuiMCP::ImVec2 size;
                ImGuiMCP::CalcTextSize(&size, Locale::Text(a_label), nullptr, false, -1.0F);
                return size.x;
            };
            const auto minimumWidth = std::max(inputWidth, textWidth("Min distance"));
            const auto maximumWidth = std::max(inputWidth, textWidth("Max distance"));
            const auto followerWidth = std::max(GetFollowerComboWidth(), textWidth("Follower"));
            const auto disconnectWidth = ImGuiMCP::GetFrameHeight();
            constexpr auto tableFlags = ImGuiMCP::ImGuiTableFlags_BordersInnerH | ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_SizingStretchProp;
            if (!ImGuiMCP::BeginTable("ActiveLeashes", 6, tableFlags)) {
                return;
            }
            ImGuiMCP::TableSetupColumn(Locale::Text("Leashed actor"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.0F);
            ImGuiMCP::TableSetupColumn(Locale::Text("Leasher"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.0F);
            ImGuiMCP::TableSetupColumn(Locale::Text("Follower"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, followerWidth);
            ImGuiMCP::TableSetupColumn(Locale::Text("Min distance"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, minimumWidth);
            ImGuiMCP::TableSetupColumn(Locale::Text("Max distance"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, maximumWidth);
            ImGuiMCP::TableSetupColumn("", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, disconnectWidth);
            ImGuiMCP::TableHeadersRow();

            for (const auto& definition : a_definitions) {
                ImGuiMCP::PushID(static_cast<int>(definition.leashedFormID));
                auto* leashed = RE::TESForm::LookupByID<RE::Actor>(definition.leashedFormID);
                ImGuiMCP::TableNextRow();
                ImGuiMCP::TableSetColumnIndex(0);
                const auto ropeOwner = std::holds_alternative<StandaloneMesh>(definition.mesh) ? std::string{Locale::Text("Standalone NIF")} : DescribeActor(definition.GetFormID(definition.GetMeshSide()));
                RenderActiveLeashActor(definition.leashedFormID, std::format("{}\n{}: {}",
                    Locale::Text(definition.persistent ? "Persistent leash / included in saves" : "Temporary leash / current session only"), Locale::Text("Physical owner"), ropeOwner));
                ImGuiMCP::TableSetColumnIndex(1);
                if (definition.holderFormID != 0) {
                    RenderActiveLeashActor(definition.holderFormID);
                } else {
                    ImGuiMCP::AlignTextToFramePadding();
                    ImGuiMCP::TextUnformatted(Locale::Text("World position"));
                    MenuLayout::Help("World position");
                }
                ImGuiMCP::TableSetColumnIndex(2);
                RenderActiveLeashFollower(leashed, definition);
                ImGuiMCP::TableSetColumnIndex(3);
                RenderActiveLeashLength(leashed, definition.minLength, true, inputWidth);
                ImGuiMCP::TableSetColumnIndex(4);
                RenderActiveLeashLength(leashed, definition.maxLength, false, inputWidth);
                ImGuiMCP::TableSetColumnIndex(5);
                if (MenuLayout::Button("Disconnect", {disconnectWidth, disconnectWidth}, "X")) {
                    auto* holder = RE::TESForm::LookupByID<RE::Actor>(definition.holderFormID);
                    const auto disconnected = LeashManager::GetSingleton().Disconnect(holder, leashed);
                    const auto leashedLabel = DescribeActor(definition.leashedFormID);
                    status = disconnected ? Locale::Format("Freed {0}.", leashedLabel) : Locale::Format("Could not free {0}.", leashedLabel);
                }
                MenuLayout::Help("Disconnect removes only this leash.");
                ImGuiMCP::PopID();
            }
            ImGuiMCP::EndTable();
        }

        void RenderTestActors() {
            if (!actorsLoaded) {
                RefreshActors();
            }
            const auto previousHolder = selectedHolder;
            const auto previousLeashed = selectedLeashed;
            const auto previousSource = GetMeshSource();
            MenuLayout::Columns(
                [] { RenderActorDropdown("Leashed actor", selectedLeashed); },
                [] {
                    if (!debugSettings.holderOwnsLeash && selectedAnchorType == DebugAnchorType::kWorldPosition) {
                        ImGuiMCP::TextUnformatted(Locale::Text("Leasher"));
                        ImGuiMCP::TextDisabled("%s", Locale::Text("None (world anchor)"));
                    } else {
                        RenderActorDropdown("Leasher", selectedHolder);
                    }
                },
                [] {
                    auto source = GetMeshSource();
                    if (MenuLayout::Choice("Physical leash owner", source, kMeshOwnerLabels)) {
                        debugSettings.holderOwnsLeash = source == MeshSource::kHolder;
                        debugSettings.standaloneRope = source == MeshSource::kStandalone;
                        if (debugSettings.holderOwnsLeash) {
                            selectedAnchorType = DebugAnchorType::kActorBone;
                        }
                    }
                    if (debugSettings.standaloneRope) {
                        MenuLayout::Note("The rope is a separate scene object; neither actor needs equipment.");
                    }
                },
                [] {
                    if (MenuLayout::Field("Nearby actors", [](const char*) {
                            const auto label = Locale::Format("Refresh nearby actors ({0})", actorOptions.size());
                            return MenuLayout::Button("RefreshActors", {-1.0F, 0.0F}, label.c_str());
                        })) {
                        RefreshActors();
                    }
                });
            if (previousHolder != selectedHolder || previousLeashed != selectedLeashed || previousSource != GetMeshSource()) {
                status.clear();
                skeletonDump.clear();
                skeletonDumpActor.clear();
            }
        }

        void RenderAnchorSettings() {
            if (debugSettings.holderOwnsLeash) {
                MenuLayout::Note("The leasher wears the rope; its endpoint attaches to a bone on the leashed actor.");
            } else {
                if (MenuLayout::Choice("Attach to", selectedAnchorType, kDebugAnchorLabels)) {
                    status.clear();
                    if (selectedAnchorType == DebugAnchorType::kWorldPosition && !CapturePlayerWorldAnchor()) {
                        status = Locale::Text("Could not capture the player position and cell.");
                    }
                }
            }

            if (!debugSettings.holderOwnsLeash && selectedAnchorType == DebugAnchorType::kWorldPosition) {
                MenuLayout::Vector("World position (X, Y, Z)", selectedWorldPosition);
                ImGuiMCP::TextUnformatted(Locale::Format("Cell: {0:08X}", selectedWorldCellFormID).c_str());
                if (MenuLayout::Button("Use current player position")) {
                    status = Locale::Text(CapturePlayerWorldAnchor() ? "Captured the current player position and cell." : "Could not capture the player position and cell.");
                }
            } else if (debugSettings.holderOwnsLeash || selectedAnchorType == DebugAnchorType::kActorBone) {
                MenuLayout::Text(debugSettings.holderOwnsLeash ? "Bone on leashed actor" : "Bone on leasher", selectedAttachmentBone, "Exact name of the bone at the rope's free end.");
                MenuLayout::StepVector("Attachment offset (X, Y, Z)", debugSettings.attachmentOffset,
                    "Offset from the selected attachment bone, in its local coordinates.");
                if (debugSettings.holderOwnsLeash) {
                    MenuLayout::Choice("Closed leasher hand", debugSettings.closedHand, kClosedHandLabels);
                }
            } else {
                MenuLayout::Note("Uses the selected leasher's hand with the closed-fist grip.");
            }
        }

        void RenderRopeSettings() {
            if (debugSettings.standaloneRope) {
                MenuLayout::Text("Rope NIF path", debugSettings.ropeModelPath, "Path relative to Data/Meshes. Include the rope skeleton and skinned geometry; omit NIF collision objects.");
                MenuLayout::Text("Bone on leashed actor", debugSettings.leashedAttachmentBone, "Exact third-person bone name for the first rope endpoint.");
                MenuLayout::StepVector("Leashed attachment offset", debugSettings.leashedAttachmentOffset, "Offset in the attachment bone's local coordinates.");
            }
            MenuLayout::Text("Parent bone", debugSettings.parentBone,
                debugSettings.standaloneRope ? "Optional parent node inside the rope NIF. Leave empty to search beneath the NIF root. Matching child bones are traversed in order." :
                "Exact parent bone name on the physical leash owner. Use the Skeleton tab to inspect available bones.");
            MenuLayout::Text("Leash bone match", debugSettings.leashBoneMatch,
                "Match text for bones beneath the parent. At least two matching bones are required.");
        }

        void RenderPullDistances() {
            MenuLayout::Columns("PullDistances", 22.0F, {
                [] {
                    MenuLayout::Number("Minimum length", debugSettings.minLength,
                        "The player regains movement control at this distance, even while the holder moves. NPC followers settle here with a small arrival tolerance when the holder stops.\n"
                        "While the holder moves, the preferred gap is configured under Locomotion. World-position leashes stop pulling at this distance.", 1.0F, 10.0F, "%.1f");
                },
                [] {
                    MenuLayout::Number("Maximum length", debugSettings.maxLength,
                        "Player pulling and world-position pulling begin only beyond this distance. Actor-held NPC followers can start earlier to keep pace.\nForced recovery also uses this length.", 1.0F, 10.0F, "%.1f");
                }
            });
        }

        [[nodiscard]] LeashAnchorDefinition GetSelectedAnchor() {
            if (debugSettings.holderOwnsLeash || selectedAnchorType == DebugAnchorType::kActorBone) {
                const auto& offset = debugSettings.attachmentOffset;
                return ActorBoneAnchor{selectedAttachmentBone, offset.x, offset.y, offset.z};
            }
            if (selectedAnchorType == DebugAnchorType::kWorldPosition) {
                return WorldPositionAnchor{selectedWorldCellFormID, selectedWorldPosition.x, selectedWorldPosition.y, selectedWorldPosition.z};
            }
            return HandAnchor{selectedAnchorType == DebugAnchorType::kRightHand};
        }

        [[nodiscard]] LeashMeshDefinition GetSelectedMesh() {
            if (debugSettings.standaloneRope) {
                const auto& offset = debugSettings.leashedAttachmentOffset;
                return StandaloneMesh{debugSettings.ropeModelPath, {debugSettings.leashedAttachmentBone, offset.x, offset.y, offset.z}};
            }
            if (debugSettings.holderOwnsLeash) {
                return HolderMesh{static_cast<ClosedHand>(debugSettings.closedHand)};
            }
            return LeashedMesh{};
        }

        [[nodiscard]] std::string DescribeAnchor(const LeashAnchorDefinition& a_anchor, RE::Actor* a_actor) {
            if (const auto* hand = std::get_if<HandAnchor>(&a_anchor)) {
                return hand->rightHand ? Locale::Format("the right hand of {0}", DescribeActor(a_actor)) : Locale::Format("the left hand of {0}", DescribeActor(a_actor));
            }
            if (const auto* bone = std::get_if<ActorBoneAnchor>(&a_anchor)) {
                return Locale::Format("bone '{0}' on {1}", bone->boneName, DescribeActor(a_actor));
            }
            const auto& world = std::get<WorldPositionAnchor>(a_anchor);
            return Locale::Format("world position ({0:.1f}, {1:.1f}, {2:.1f}) in cell {3:08X}", world.x, world.y, world.z, world.cellFormID);
        }

        void ApplyTestLeash() {
            auto* leashed = RE::TESForm::LookupByID<RE::Actor>(selectedLeashed);
            auto* holder = RE::TESForm::LookupByID<RE::Actor>(selectedHolder);
            const auto anchor = GetSelectedAnchor();
            const auto* worldAnchor = std::get_if<WorldPositionAnchor>(&anchor);
            if (!leashed) {
                status = Locale::Text("Could not apply leash. Select an available leashed actor.");
                return;
            }
            if (worldAnchor && !RE::TESForm::LookupByID<RE::TESObjectCELL>(worldAnchor->cellFormID)) {
                status = Locale::Text("Could not apply leash. Capture an available player position and cell.");
                return;
            }
            if (!worldAnchor && !holder) {
                status = Locale::Text("Could not apply leash. Select an available leasher.");
                return;
            }

            const auto anchorLabel = DescribeAnchor(anchor, debugSettings.holderOwnsLeash ? leashed : holder);
            const auto applied = LeashManager::GetSingleton().ApplyDefinition({.holderFormID = worldAnchor ? 0 : selectedHolder,
                .leashedFormID = selectedLeashed,
                .mesh = GetSelectedMesh(),
                .anchor = anchor,
                .parentBone = debugSettings.parentBone,
                .leashBoneMatch = debugSettings.leashBoneMatch,
                .minLength = debugSettings.minLength,
                .maxLength = debugSettings.maxLength,
                .persistent = debugSettings.persistent,
                .follower = worldAnchor ? LeashSide::kLeashed : static_cast<LeashSide>(debugSettings.follower)});
            if (debugSettings.standaloneRope) {
                status = Locale::Text(applied ? "Standalone leash created. No rope equipment is needed; inspect its bones in Skeleton." :
                    "Could not create standalone rope. Requires loaded actors/cell, valid bone names and a self-contained NIF. Check the plugin log.");
                return;
            }
            if (!applied) {
                status = Locale::Text("Could not apply leash. Check the selected anchor, bone names, and length values.");
                return;
            }

            auto* meshOwner = debugSettings.holderOwnsLeash ? holder : leashed;
            auto* root = meshOwner->Get3D(false);
            auto* parent = root ? root->GetObjectByName(RE::BSFixedString(debugSettings.parentBone)) : nullptr;
            auto* parentNode = parent ? parent->AsNode() : nullptr;
            const std::string_view leashMatch{debugSettings.leashBoneMatch};
            const auto matchedBones = parentNode ? SceneGraph::CollectBones(*parentNode, leashMatch).size() : 0;
            if (!root) {
                status = Locale::Format("Warning: Leash applied, but {0} has no currently loaded third-person skeleton.", DescribeActor(meshOwner));
            } else if (!parent) {
                status = Locale::Format("Warning: Leash applied, but {0} does not currently contain parent bone '{1}'.", DescribeActor(meshOwner), debugSettings.parentBone);
            } else if (!parentNode) {
                status = Locale::Format("Warning: Leash applied, but parent bone '{0}' on {1} is not a node.", debugSettings.parentBone, DescribeActor(meshOwner));
            } else if (matchedBones == 0) {
                status = Locale::Format("Warning: Leash applied, but {0} does not currently contain a bone matching '{1}' under '{2}'.", DescribeActor(meshOwner), leashMatch, debugSettings.parentBone);
            } else if (matchedBones == 1) {
                status = Locale::Format("Warning: Leash applied, but {0} currently contains only one bone matching '{1}' under '{2}'; at least two are required to bind.", DescribeActor(meshOwner),
                    leashMatch, debugSettings.parentBone);
            } else if (!debugSettings.holderOwnsLeash) {
                status = Locale::Format("Leashed {0} to {1}.", DescribeActor(leashed), anchorLabel);
            } else if (auto* attachmentRoot = leashed->Get3D(false); !attachmentRoot || !attachmentRoot->GetObjectByName(RE::BSFixedString(selectedAttachmentBone))) {
                status = Locale::Format("Warning: Leash applied, but {0} does not currently contain attachment bone '{1}'.", DescribeActor(leashed), selectedAttachmentBone);
            } else {
                status = Locale::Format("Leashed {0} to {1} using the leash equipped by {2}.", DescribeActor(leashed), anchorLabel, DescribeActor(holder));
            }
        }

        [[nodiscard]] TestLeashPreset CaptureTestLeashPreset(std::string a_name) {
            return {.name = std::move(a_name),
                .holderOwnsLeash = debugSettings.holderOwnsLeash,
                .standaloneRope = debugSettings.standaloneRope,
                .anchorType = static_cast<int>(selectedAnchorType),
                .attachmentBone = selectedAttachmentBone,
                .attachmentOffset = debugSettings.attachmentOffset,
                .closedHand = debugSettings.closedHand,
                .ropeModelPath = debugSettings.ropeModelPath,
                .leashedAttachmentBone = debugSettings.leashedAttachmentBone,
                .leashedAttachmentOffset = debugSettings.leashedAttachmentOffset,
                .parentBone = debugSettings.parentBone,
                .leashBoneMatch = debugSettings.leashBoneMatch,
                .minLength = debugSettings.minLength,
                .maxLength = debugSettings.maxLength,
                .persistent = debugSettings.persistent,
                .follower = debugSettings.follower};
        }

        void LoadTestLeashPreset(const TestLeashPreset& a_preset) {
            const auto previousSource = GetMeshSource();
            debugSettings.standaloneRope = a_preset.standaloneRope;
            debugSettings.holderOwnsLeash = a_preset.holderOwnsLeash && !a_preset.standaloneRope;
            const bool validAnchor = a_preset.anchorType >= 0 && a_preset.anchorType < static_cast<int>(kDebugAnchorLabels.size());
            selectedAnchorType = debugSettings.holderOwnsLeash ? DebugAnchorType::kActorBone : validAnchor ? static_cast<DebugAnchorType>(a_preset.anchorType) : DebugAnchorType::kRightHand;
            CopyText(selectedAttachmentBone, a_preset.attachmentBone);
            debugSettings.attachmentOffset = a_preset.attachmentOffset;
            debugSettings.closedHand = a_preset.closedHand == 1 || a_preset.closedHand == 2 ? a_preset.closedHand : 0;
            CopyText(debugSettings.ropeModelPath, a_preset.ropeModelPath);
            CopyText(debugSettings.leashedAttachmentBone, a_preset.leashedAttachmentBone);
            debugSettings.leashedAttachmentOffset = a_preset.leashedAttachmentOffset;
            CopyText(debugSettings.parentBone, a_preset.parentBone);
            CopyText(debugSettings.leashBoneMatch, a_preset.leashBoneMatch);
            debugSettings.minLength = a_preset.minLength;
            debugSettings.maxLength = a_preset.maxLength;
            debugSettings.persistent = a_preset.persistent;
            debugSettings.follower = a_preset.follower == static_cast<int>(LeashSide::kHolder) ? a_preset.follower : static_cast<int>(LeashSide::kLeashed);

            if (previousSource != GetMeshSource()) {
                skeletonDump.clear();
                skeletonDumpActor.clear();
            }
            status = Locale::Format("Loaded preset '{0}'.", a_preset.name);
            if (selectedAnchorType == DebugAnchorType::kWorldPosition && !debugSettings.holderOwnsLeash && !CapturePlayerWorldAnchor()) {
                status = Locale::Format("Loaded preset '{0}', but could not capture the player position and cell.", a_preset.name);
            }
        }

        [[nodiscard]] float ButtonWidth(const char* a_label) {
            ImGuiMCP::ImVec2 size;
            ImGuiMCP::CalcTextSize(&size, Locale::Text(a_label), nullptr, false, -1.0F);
            return size.x + ImGuiMCP::GetStyle()->FramePadding.x * 2.0F;
        }

        void RenderPresets() {
            auto& presets = debugSettings.testLeashPresets;
            if (selectedPreset != kNoPreset && selectedPreset >= presets.size()) {
                selectedPreset = kNoPreset;
                confirmPresetDelete = false;
            }
            const auto* selected = selectedPreset != kNoPreset ? &presets[selectedPreset] : nullptr;
            const auto spacing = ImGuiMCP::GetStyle()->ItemSpacing.x;

            bool loadRequested{};
            MenuLayout::Field("Saved presets", [&](const char* a_id) {
                ImGuiMCP::ImVec2 available;
                ImGuiMCP::GetContentRegionAvail(&available);
                const auto buttonsWidth = ButtonWidth("Load") + ButtonWidth("Delete") + spacing * 2.0F;
                ImGuiMCP::SetNextItemWidth(std::max(available.x - buttonsWidth, ImGuiMCP::GetFontSize() * 8.0F));
                const char* preview = selected ? selected->name.c_str() : Locale::Text(presets.empty() ? "No saved presets" : "Select preset");
                bool changed{};
                if (ImGuiMCP::BeginCombo(a_id, preview)) {
                    for (std::uint32_t index = 0; index < presets.size(); ++index) {
                        changed |= MenuLayout::ChoiceItem(presets[index].name.c_str(), index, selectedPreset);
                    }
                    ImGuiMCP::EndCombo();
                }
                if (changed) {
                    CopyText(presetName, presets[selectedPreset].name);
                    confirmPresetDelete = false;
                    selected = &presets[selectedPreset];
                }
                ImGuiMCP::BeginDisabled(!selected);
                ImGuiMCP::SameLine();
                loadRequested = MenuLayout::Button("Load");
                MenuLayout::Help("Replace the test leash settings below with this preset. Selected actors are not changed.");
                ImGuiMCP::SameLine();
                if (MenuLayout::Button("Delete")) {
                    confirmPresetDelete = true;
                }
                ImGuiMCP::EndDisabled();
                return changed;
            });
            if (loadRequested && selected) {
                LoadTestLeashPreset(*selected);
            }

            if (confirmPresetDelete && selected) {
                MenuLayout::NoteRaw(Locale::Format("Delete preset '{0}'? This cannot be undone.", selected->name).c_str());
                if (MenuLayout::Button("Confirm delete")) {
                    status = Locale::Format("Deleted preset '{0}'.", selected->name);
                    presets.erase(presets.begin() + selectedPreset);
                    selectedPreset = kNoPreset;
                    selected = nullptr;
                    confirmPresetDelete = false;
                    ModMenu::SaveSettings();
                }
                ImGuiMCP::SameLine();
                if (MenuLayout::Button("Cancel")) {
                    confirmPresetDelete = false;
                }
            }

            std::string_view name{presetName};
            name.remove_prefix(std::min(name.find_first_not_of(" \t"), name.size()));
            name.remove_suffix(name.size() - std::min(name.find_last_not_of(" \t") + 1, name.size()));
            const auto existing = std::ranges::find(presets, name, &TestLeashPreset::name);
            const char* saveLabel = existing != presets.end() ? "Overwrite###SavePreset" : "Save###SavePreset";
            bool saveRequested{};
            MenuLayout::Field("Preset name", [&](const char* a_id) {
                ImGuiMCP::ImVec2 available;
                ImGuiMCP::GetContentRegionAvail(&available);
                const auto buttonWidth = std::max(ButtonWidth("Save"), ButtonWidth("Overwrite"));
                ImGuiMCP::SetNextItemWidth(std::max(available.x - buttonWidth - spacing, ImGuiMCP::GetFontSize() * 8.0F));
                const bool changed = ImGuiMCP::InputTextWithHint(a_id, Locale::Text("Name this configuration"), presetName, sizeof(presetName));
                ImGuiMCP::SameLine();
                ImGuiMCP::BeginDisabled(name.empty());
                saveRequested = MenuLayout::Button(saveLabel, {buttonWidth, 0.0F});
                ImGuiMCP::EndDisabled();
                MenuLayout::Help("Save the current test leash settings under this name. Using an existing name overwrites that preset.");
                return changed;
            });
            if (saveRequested && !name.empty()) {
                auto preset = CaptureTestLeashPreset(std::string{name});
                if (existing != presets.end()) {
                    *existing = std::move(preset);
                    selectedPreset = static_cast<std::uint32_t>(existing - presets.begin());
                    status = Locale::Format("Overwrote preset '{0}'.", name);
                } else {
                    presets.push_back(std::move(preset));
                    selectedPreset = static_cast<std::uint32_t>(presets.size() - 1);
                    status = Locale::Format("Saved preset '{0}'.", name);
                }
                confirmPresetDelete = false;
                ModMenu::SaveSettings();
            }
            MenuLayout::Note("Presets store the rope, anchor, bone and distance settings. Actors and world coordinates are chosen when loading.");
        }

        void RenderTestLeashFollower() {
            const auto worldAnchor = !debugSettings.holderOwnsLeash && selectedAnchorType == DebugAnchorType::kWorldPosition;
            auto index = worldAnchor ? static_cast<int>(LeashSide::kLeashed) : debugSettings.follower;
            // Push the label and dropdown against the panels right edge
            ImGuiMCP::ImVec2 label;
            ImGuiMCP::CalcTextSize(&label, Locale::Text("Follower"), nullptr, false, -1.0F);
            const auto width = label.x + ImGuiMCP::GetStyle()->ItemSpacing.x + GetFollowerComboWidth();
            ImGuiMCP::ImVec2 available;
            ImGuiMCP::GetContentRegionAvail(&available);
            if (available.x > width) {
                ImGuiMCP::SetCursorPosX(ImGuiMCP::GetCursorPosX() + available.x - width);
            }
            ImGuiMCP::BeginDisabled(worldAnchor);
            ImGuiMCP::AlignTextToFramePadding();
            ImGuiMCP::TextUnformatted(Locale::Text("Follower"));
            ImGuiMCP::SameLine();
            if (FollowerCombo(index)) {
                debugSettings.follower = index;
            }
            ImGuiMCP::EndDisabled();
            MenuLayout::Help(kFollowerHelp);
        }

        void RenderApplyLeash() {
            bool applyRequested{};
            MenuLayout::Panel("Presets", "Save or load test leash configurations.", RenderPresets);
            MenuLayout::Panel("01 / Actors", "Choose the actors and rope source.", RenderTestActors);
            const auto anchor = [] { MenuLayout::Panel("02 / Anchor", "Choose where the free end of the rope attaches.", RenderAnchorSettings); };
            const auto rope = [] {
                MenuLayout::Panel("03 / Rope bones & range", "Identify the rope and set its follow distances.", [] {
                    RenderRopeSettings();
                    ImGuiMCP::Separator();
                    RenderPullDistances();
                });
            };
            const auto apply = [&] {
                MenuLayout::Panel("04 / Apply test leash", debugSettings.standaloneRope ? "Spawn the configured NIF without equipping armor." : "Equip a rope in Equipment; check its bones in Skeleton.", [&] {
                    MenuLayout::Toggle("Keep leash in saves", debugSettings.persistent, "Save this leash with the game. Temporary test leashes are discarded when loading.");
                    ImGuiMCP::SameLine();
                    RenderTestLeashFollower();
                    applyRequested = MenuLayout::Button("Apply test leash", {-1.0F, 0.0F});
                });
            };
            ImGuiMCP::ImVec2 available;
            ImGuiMCP::GetContentRegionAvail(&available);
            if (available.x >= ImGuiMCP::GetFontSize() * 56.0F) {
                MenuLayout::Columns([&] { anchor(); apply(); }, rope);
            } else {
                anchor();
                rope();
                apply();
            }
            if (applyRequested) {
                ApplyTestLeash();
            }
        }

        void RenderDiagnostics() {
            MenuLayout::Columns(
                [] {
                    MenuLayout::Panel("Collision overlay", "Visualize the body shapes used for actor collision.", [] {
                        if (MenuLayout::Toggle("Draw actor collision", actorCollisionDebugEnabled,
                                "Draw the actor collision shapes for debugging. This does not enable or disable physical collision.") && actorCollisionDebugEnabled) {
                            DebugOverlay::Register();
                        }
                        MenuLayout::Note("Configure body shapes in Settings > Body collision.");
                    });
                },
                [] {
                    MenuLayout::Panel("Pull diagnostics", "Record detailed pull-controller information in the plugin log.", [] {
                        if (MenuLayout::Toggle("Enable debug logging", debugSettings.enablePullDiagnostics, "Enable detailed pull diagnostics. This preference is saved when the menu closes.")) {
                            LeashManager::GetSingleton().SetPullDiagnosticsEnabled(debugSettings.enablePullDiagnostics);
                        }
                        static const std::string logFilePath = [] {
                            if (const auto directory = SKSE::log::log_directory()) {
                                return (*directory / "LeashFramework.log").string();
                            }
                            return std::string{R"(C:\Users\%USERNAME%\Documents\My Games\Skyrim Special Edition\SKSE\LeashFramework.log)"};
                        }();
                        ImGuiMCP::Spacing();
                        MenuLayout::Note("Log file");
                        ImGuiMCP::TextWrapped("%s", logFilePath.c_str());
                        if (MenuLayout::Button("Copy log file path")) {
                            ImGuiMCP::SetClipboardText(logFilePath.c_str());
                        }
                    });
                });
        }

        void RenderEquipment() {
            MenuLayout::Panel("Actors & mesh owner", "Equipment is applied to the selected physical leash owner.", RenderTestActors);
            if (debugSettings.standaloneRope) {
                MenuLayout::Note("Standalone ropes do not use equipment. Configure a NIF in Test leash.");
                return;
            }
            const auto ownerID = debugSettings.holderOwnsLeash ? selectedHolder : selectedLeashed;
            ImGuiMCP::TextWrapped("%s", Locale::Format("Equip on: {0}", DescribeActor(ownerID)).c_str());
            MenuLayout::Columns(
                [] { MenuLayout::Panel("Browse Leash.esm", "Choose armor supplied with Leash Framework.", RenderPluginArmorDropdown); },
                [] { MenuLayout::Panel("Armor shortcuts", "Equip a saved favorite or edit its plugin and FormID.", RenderArmorEntries); });
        }
    }  // namespace

    DebugSettings GetSettings() { return debugSettings; }

    void SetSettings(const DebugSettings& a_settings) {
        debugSettings = a_settings;
        if (debugSettings.standaloneRope) {
            debugSettings.holderOwnsLeash = false;
        }
        if (debugSettings.closedHand != 1 && debugSettings.closedHand != 2) {
            debugSettings.closedHand = 0;
        }
        LeashManager::GetSingleton().SetPullDiagnosticsEnabled(debugSettings.enablePullDiagnostics);
    }

    bool IsActorCollisionDebugEnabled() { return actorCollisionDebugEnabled; }

    void Render() {
        const MenuLayout::Style style;
        ImGuiMCP::PushID("LeashDebug");
        MenuLayout::Title("LEASH FRAMEWORK / DEBUG", "Create a test leash, inspect active leashes, and explore equipment and bones. Hover labels or controls for details.");
        MenuLayout::Status(status);
        ImGuiMCP::Spacing();
        if (ImGuiMCP::BeginTabBar("DebugTools", ImGuiMCP::ImGuiTabBarFlags_FittingPolicyScroll)) {
            MenuLayout::Tab("Test leash", RenderApplyLeash);
            const auto definitions = LeashManager::GetSingleton().GetDefinitions();
            const auto activeLabel = Locale::Format("Active leashes ({0})", definitions.size());
            MenuLayout::Tab("ActiveLeashesTab", [&] {
                RenderActiveLeashes(definitions);
            }, activeLabel.c_str());
            MenuLayout::Tab("Equipment", RenderEquipment);
            MenuLayout::Tab("Skeleton", [] {
                MenuLayout::Panel("Actors & mesh owner", "Select the physical leash owner whose skeleton you want to inspect.", RenderTestActors);
                RenderSkeletonDumper();
            });
            MenuLayout::Tab("Diagnostics", RenderDiagnostics);
            ImGuiMCP::EndTabBar();
        }
        ImGuiMCP::PopID();
    }
}  // namespace LeashFramework::UI::DebugPage
