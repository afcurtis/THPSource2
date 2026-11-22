#pragma once

#include "object_script.hpp"
#include "trg_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace opentony::trg {

// Native model of the retail level-object lists (ob.cpp, trig.cpp).
//
// Retail keeps every live level object on one of a few intrusive doubly-linked
// lists (baddies, game objects, power-ups) and moves suspended objects onto a
// shared "active" list. The list operations are reproduced link for link,
// including the retail habit of leaving a removed object's own next pointer
// untouched: a loop that suspends the object it is standing on continues into
// the list that object just joined.
//
// Addresses: ObjectList_PushHead 0x004801d0, ObjectList_Remove 0x004801f0,
// Object_SuspendToList 0x004802f0, CBody__UnSuspend 0x004803b0, Ob_AI
// 0x00480ff0, SendSignalToNode 0x004c5b30, TRG_SendSignalToNodes 0x004c5b60,
// TRG_SuspendObjectByNode 0x004c5c00, SendUnSuspend 0x004c5c40,
// TRG_ApplyActivationOrSuspendList 0x004c5c70, KillInList 0x004c7bf0,
// TRG_SendKillFromNode 0x004c7a00, Utils_CrapDist 0x004c9620.

using ObjectId = std::int32_t;
inline constexpr ObjectId kNoObject = -1;

enum class ObjectListId : std::uint8_t { Baddy, GameObject, Suspended, Powerup, Count };

// list_flags (+0x6a) bits used by the object loop and the list operations.
inline constexpr std::uint16_t kObjectSuspended = 0x0001;
inline constexpr std::uint16_t kObjectSuspendWhenFar = 0x0002;
inline constexpr std::uint16_t kObjectDead = 0x0040;
inline constexpr std::uint16_t kObjectDeathSeen = 0x0080;

// Distance beyond which a suspend-when-far object is parked (DAT_00535af8).
inline constexpr std::uint32_t kSuspendDistance = 0x32c8;

// Utils_CrapDist: an octagonal distance approximation, in whole world units.
[[nodiscard]] std::uint32_t crap_distance(const ScriptVec3& a, const ScriptVec3& b) noexcept;

// A level object as the lists see it: the script-visible fields (node,
// event word, list flags, state, position) plus the list links. Objects that
// are not script objects leave the script-only fields at their defaults.
struct WorldObject : ScriptObject {
    ScriptClass klass{ScriptClass::ScriptObjectCb};  // which virtual table the object has
    ObjectId next{kNoObject};         // +0x20
    ObjectId prev{kNoObject};         // +0x34
    ObjectListId owner{ObjectListId::Baddy};  // +0x64 list to rejoin when unsuspended
    bool destroyed{};
};

// Behaviour supplied by the object's class (virtual slots in retail) and by
// the surrounding game.
class WorldObjectHooks {
public:
    virtual ~WorldObjectHooks() = default;
    virtual void on_assertion(std::string_view) {}
    virtual void die(ObjectId) {}                   // vtable +0x08
    virtual void hit(ObjectId) {}                   // vtable +0x18 (KillInList mode 1)
    virtual void update(ObjectId) {}                // vtable +0x0c
    virtual void destroy(ObjectId) {}               // vtable +0x00 with the delete flag
    virtual void advance_animation(ObjectId) {}     // Animation_Advance
    [[nodiscard]] virtual bool video_playing() const { return false; }
    [[nodiscard]] virtual std::size_t player_count() const { return 1; }
    [[nodiscard]] virtual ScriptVec3 player_position(std::size_t) const { return {}; }
    // Type 2/9/10/11/501 targets of a kill live in other systems.
    virtual void kill_special_node(std::uint16_t /*node*/, std::uint16_t /*type*/, int /*mode*/) {}
    // The platform (0x192) path of SendKill; returns true when it consumed the kill.
    [[nodiscard]] virtual bool kill_platform_at_node(std::uint16_t /*node*/, int /*mode*/) { return false; }
};

class WorldObjectPool final {
public:
    explicit WorldObjectPool(WorldObjectHooks& hooks) : hooks_(hooks) {}

    // Allocate an object and push it on the head of `list`.
    ObjectId create(ObjectListId list, WorldObject prototype = {});

    [[nodiscard]] WorldObject& at(ObjectId id) { return objects_.at(static_cast<std::size_t>(id)); }
    [[nodiscard]] const WorldObject& at(ObjectId id) const { return objects_.at(static_cast<std::size_t>(id)); }
    [[nodiscard]] std::size_t size() const noexcept { return objects_.size(); }
    [[nodiscard]] ObjectId head(ObjectListId list) const noexcept { return heads_[index(list)]; }
    // Objects of a list in head-to-tail order.
    [[nodiscard]] std::vector<ObjectId> members(ObjectListId list) const;
    [[nodiscard]] ObjectId find_node(ObjectListId list, std::uint16_t node) const;

    void push_head(ObjectListId list, ObjectId id);
    void remove(ObjectListId list, ObjectId id);
    void suspend(ObjectListId from, ObjectId id);
    void unsuspend(ObjectId id);

    // SendSignalToNode
    void signal_node(ObjectListId list, std::uint16_t node);
    // TRG_SuspendObjectByNode / SendUnSuspend
    void suspend_by_node(ObjectListId list, std::uint16_t node);
    void unsuspend_by_node(std::uint16_t node);
    // KillInList: mode 0 kills, mode 1 hits.
    void kill_in_list(std::uint16_t node, ObjectListId list, int mode);

    // Ob_AI: the per-frame loop over one list.
    void run_frame(ObjectListId list);
    // Ob_QuickAI (0x00481110): the loop of the game-object list. No distance
    // bookkeeping and no far-away suspension; every live object updates.
    void run_quick_frame(ObjectListId list);
    // Ob_ClearInputFlags (0x00480fd0): keep only bit 0 of every event word.
    void clear_input_flags(ObjectListId list);
    // Ob_MaybeUnSuspendOrCull (0x00481150): wake suspended objects that are
    // flagged suspend-when-far and have come within range of a player.
    void wake_near_objects();

    // The object's own destructor: leave the list it is on and free the slot.
    void release(ObjectId id, ObjectListId list);

private:
    WorldObjectHooks& hooks_;
    std::deque<WorldObject> objects_;  // stable references while hooks create objects
    std::array<ObjectId, 4> heads_{kNoObject, kNoObject, kNoObject, kNoObject};

    [[nodiscard]] static std::size_t index(ObjectListId list) noexcept {
        return static_cast<std::size_t>(list);
    }
    void assert_that(bool condition, std::string_view message) {
        if (!condition) {
            hooks_.on_assertion(message);
        }
    }
};

// The TRG message commands (send signal / activate / suspend / kill) for the
// baddy and game-object lists.
class WorldMessenger final {
public:
    WorldMessenger(const TrgFile& file, WorldObjectPool& pool, WorldObjectHooks& hooks)
        : file_(file), pool_(pool), hooks_(hooks) {}

    void signal_nodes(std::span<const std::uint16_t> links);
    // opcode 4 activates, opcode 5 suspends.
    void activate_or_suspend(std::span<const std::uint16_t> links, std::uint16_t opcode);
    // opcode 0x0b kills (mode 0), 0x0c hits (mode 1).
    void kill_from_node(std::span<const std::uint16_t> links, int mode);

private:
    const TrgFile& file_;
    WorldObjectPool& pool_;
    WorldObjectHooks& hooks_;
};

} // namespace opentony::trg
