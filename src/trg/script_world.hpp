#pragma once

#include "object_script.hpp"
#include "world_objects.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace opentony::trg {

// What the script world needs from the game around it: the script services
// (sound, effects, career flags, ...) plus player state and the frame time.
class ScriptMessageSink {
public:
    virtual ~ScriptMessageSink() = default;
    virtual void signal_links(std::uint16_t node) = 0;
    virtual void execute_links(std::uint16_t node) = 0;
    virtual void signal_objects(std::uint16_t node) = 0;
};

// The three node-messaging script commands act on the world's own objects, so
// the environment routes them to the attached ScriptWorld. Everything else is
// overridden by the game (or a test) as usual; send_pulse is the outward
// pulse into the TRG runtime.
class ScriptWorldEnvironment : public ObjectScriptServices {
public:
    [[nodiscard]] virtual std::size_t player_count() const { return 1; }
    [[nodiscard]] virtual ScriptVec3 player_position(std::size_t) const { return {}; }
    // The skater's velocity and context (retail DAT_0056a960's +0x4c and the
    // context word passed to SFX_Grunt): bouncy props launch from them.
    [[nodiscard]] virtual ScriptVec3 player_velocity() const { return {}; }
    [[nodiscard]] virtual std::uint32_t player_context() const { return 0; }
    [[nodiscard]] virtual std::uint32_t frame_delta() const { return 0; }
    // The hidden bits TRG_SendVisible sets or clears (0x41 with one player).
    [[nodiscard]] virtual std::uint16_t hidden_mask() const { return 0x0041; }

    void signal_node_links(std::uint16_t node) final {
        if (sink_ != nullptr) {
            sink_->signal_links(node);
        }
    }
    void execute_node_links(std::uint16_t node) final {
        if (sink_ != nullptr) {
            sink_->execute_links(node);
        }
    }
    void signal_objects_at_node(std::uint16_t node) final {
        if (sink_ != nullptr) {
            sink_->signal_objects(node);
        }
    }

private:
    friend class ScriptWorld;
    ScriptMessageSink* sink_{};
};

// The scripted level objects of one TRG file: the subtype-0xcb script objects
// (baddy list) and the subtype-0x0192 platforms and bouncy props (game-object
// list). Creation on pulse (TRG_CreateObject 0x004c5460), the shared object
// loops (Ob_AI 0x00480ff0 and Ob_QuickAI 0x00481110), the script updates, and
// the TRG message commands.
class ScriptWorld final {
public:
    ScriptWorld(const TrgFile& file, ScriptWorldEnvironment& environment);
    ~ScriptWorld();
    ScriptWorld(const ScriptWorld&) = delete;
    ScriptWorld& operator=(const ScriptWorld&) = delete;

    // A pulse reached `node`. Creates the object for a type-1/type-7 node of
    // the 0xcb or 0x0192 family (suspended unless its option list carries
    // option 2) and returns its id; any other node returns kNoObject.
    ObjectId pulse_node(std::size_t node);

    // One frame of the object lists in retail order: clear the input flags,
    // the game-object list (Ob_QuickAI), wake suspended objects that came in
    // range, then the baddy list (Ob_AI: distance bookkeeping, far-away
    // suspension, script update, and the two-frame death/delete sequence).
    void run_frame();

    // Platform_HandleBounce (0x004a12d0): the skater moved from `previous` to
    // `position` with `velocity`; every bouncy prop whose model box the swept
    // skater box touches is thrown.
    void probe_bounce(const ScriptVec3& previous, const ScriptVec3& position, const ScriptVec3& velocity);

    // TRG_SendVisible (0x004c77f0) for power-up targets: a non-zero `value`
    // clears the hidden bits of their state word, zero sets them.
    void visible(std::span<const std::uint16_t> links, std::uint16_t value);

    // A skater touched a power-up (the retail line-to-sphere test, which is not
    // reproduced here, found it): CPowerUp__TakeEffect.
    void collect_powerup(ObjectId id, std::uint32_t skater, bool primary_skater);

    // The message commands, taking the source node's link list.
    void signal(std::span<const std::uint16_t> links);
    void activate_or_suspend(std::span<const std::uint16_t> links, std::uint16_t opcode);
    void kill(std::span<const std::uint16_t> links, int mode);

    [[nodiscard]] WorldObjectPool& pool() noexcept;
    [[nodiscard]] const WorldObjectPool& pool() const noexcept;
    [[nodiscard]] std::size_t assertion_count() const noexcept;
    [[nodiscard]] std::size_t faulted_updates() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace opentony::trg
