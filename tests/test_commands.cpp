// Undo/redo, including the ownership handshake for host-allocated commands.

#include "TestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cadgeom;

namespace {

/// Bookkeeping shared with the test body: the stack owns the command objects,
/// so the only way to observe their lifetime is from outside.
struct Ledger {
    int executed = 0;
    int undone = 0;
    int released = 0;
    int value = 0;
};

/// Adds `delta` to the ledger's value; undo subtracts it back out.
class AddCommand final : public ICommand {
public:
    AddCommand(Ledger& ledger, int delta, const char* name = "Add")
        : ledger_(ledger), delta_(delta), name_(name) {}

    void Release() override {
        ++ledger_.released;
        delete this;
    }

    CgResult Execute(IScene*) override {
        ++ledger_.executed;
        ledger_.value += delta_;
        return CgResult::Ok;
    }

    CgResult Undo(IScene*) override {
        ++ledger_.undone;
        ledger_.value -= delta_;
        return CgResult::Ok;
    }

    const char* GetName() const override { return name_.c_str(); }

private:
    ~AddCommand() override = default;

    Ledger& ledger_;
    int delta_;
    std::string name_;
};

/// Fails on Execute without touching anything — the contract says such a
/// command must be released and must not reach the stack.
class FailingCommand final : public ICommand {
public:
    explicit FailingCommand(Ledger& ledger) : ledger_(ledger) {}

    void Release() override {
        ++ledger_.released;
        delete this;
    }

    CgResult Execute(IScene*) override { return CgResult::GeometryError; }
    CgResult Undo(IScene*) override { return CgResult::Ok; }
    const char* GetName() const override { return "Failing"; }

private:
    ~FailingCommand() override = default;
    Ledger& ledger_;
};

} // namespace

TEST_CASE("a fresh stack has nothing to undo", "[commands]") {
    cgtest::EngineFixture fixture;
    ICommandStack& stack = *fixture.Scene().GetCommandStack();

    CHECK_FALSE(stack.CanUndo());
    CHECK_FALSE(stack.CanRedo());
    CHECK(stack.PeekUndoName() == nullptr);
    CHECK(stack.Undo() == CgResult::InvalidState);
    CHECK(stack.Redo() == CgResult::InvalidState);
    CHECK(stack.Push(nullptr) == CgResult::InvalidArgument);
}

TEST_CASE("push executes, undo reverses, redo replays", "[commands]") {
    cgtest::EngineFixture fixture;
    ICommandStack& stack = *fixture.Scene().GetCommandStack();
    Ledger ledger;

    REQUIRE(CgSucceeded(stack.Push(new AddCommand(ledger, 5))));
    CHECK(ledger.executed == 1);
    CHECK(ledger.value == 5);
    CHECK(stack.CanUndo());
    CHECK(std::string(stack.PeekUndoName()) == "Add");

    REQUIRE(CgSucceeded(stack.Undo()));
    CHECK(ledger.value == 0);
    CHECK(ledger.undone == 1);
    CHECK_FALSE(stack.CanUndo());
    CHECK(stack.CanRedo());

    REQUIRE(CgSucceeded(stack.Redo()));
    CHECK(ledger.value == 5);
    CHECK(ledger.executed == 2);
    CHECK(stack.GetUndoCount() == 1);
    CHECK(stack.GetRedoCount() == 0);
}

TEST_CASE("a new command discards the redo branch", "[commands]") {
    cgtest::EngineFixture fixture;
    ICommandStack& stack = *fixture.Scene().GetCommandStack();
    Ledger ledger;

    stack.Push(new AddCommand(ledger, 1));
    stack.Push(new AddCommand(ledger, 2));
    stack.Undo();
    REQUIRE(stack.GetRedoCount() == 1);

    stack.Push(new AddCommand(ledger, 4));
    CHECK(stack.GetRedoCount() == 0);
    // The abandoned branch must be freed, not orphaned.
    CHECK(ledger.released == 1);
    CHECK(ledger.value == 5);
}

TEST_CASE("a command that fails to execute is released and not pushed", "[commands]") {
    cgtest::EngineFixture fixture;
    ICommandStack& stack = *fixture.Scene().GetCommandStack();
    Ledger ledger;

    CHECK(stack.Push(new FailingCommand(ledger)) == CgResult::GeometryError);
    CHECK(ledger.released == 1);
    CHECK_FALSE(stack.CanUndo());
    CHECK(stack.GetUndoCount() == 0);
}

TEST_CASE("a group collapses into one undo step", "[commands][group]") {
    cgtest::EngineFixture fixture;
    ICommandStack& stack = *fixture.Scene().GetCommandStack();
    Ledger ledger;

    stack.BeginGroup("Drag");
    stack.Push(new AddCommand(ledger, 1));
    stack.Push(new AddCommand(ledger, 2));
    stack.Push(new AddCommand(ledger, 4));
    stack.EndGroup();

    CHECK(ledger.value == 7);
    CHECK(stack.GetUndoCount() == 1);
    CHECK(std::string(stack.PeekUndoName()) == "Drag");

    REQUIRE(CgSucceeded(stack.Undo()));
    // One user gesture, one Ctrl+Z, all three reversed.
    CHECK(ledger.value == 0);
    CHECK(ledger.undone == 3);

    REQUIRE(CgSucceeded(stack.Redo()));
    CHECK(ledger.value == 7);
}

TEST_CASE("undo inside a group is refused rather than half-applied", "[commands][group]") {
    cgtest::EngineFixture fixture;
    ICommandStack& stack = *fixture.Scene().GetCommandStack();
    Ledger ledger;

    stack.Push(new AddCommand(ledger, 1));
    stack.BeginGroup("Open");
    stack.Push(new AddCommand(ledger, 2));

    CHECK_FALSE(stack.CanUndo());
    CHECK(stack.Undo() == CgResult::InvalidState);

    stack.EndGroup();
    CHECK(stack.CanUndo());
    CHECK(stack.GetUndoCount() == 2);
}

TEST_CASE("nested groups produce a single entry", "[commands][group]") {
    cgtest::EngineFixture fixture;
    ICommandStack& stack = *fixture.Scene().GetCommandStack();
    Ledger ledger;

    stack.BeginGroup("Outer");
    stack.Push(new AddCommand(ledger, 1));
    stack.BeginGroup("Inner");
    stack.Push(new AddCommand(ledger, 2));
    stack.EndGroup();
    stack.Push(new AddCommand(ledger, 4));
    stack.EndGroup();

    CHECK(stack.GetUndoCount() == 1);
    CHECK(std::string(stack.PeekUndoName()) == "Outer");
    stack.Undo();
    CHECK(ledger.value == 0);
}

TEST_CASE("an empty group leaves no trace", "[commands][group]") {
    cgtest::EngineFixture fixture;
    ICommandStack& stack = *fixture.Scene().GetCommandStack();

    // A gesture the user started and cancelled should not add a no-op entry.
    stack.BeginGroup("Cancelled");
    stack.EndGroup();
    CHECK(stack.GetUndoCount() == 0);
    CHECK_FALSE(stack.CanUndo());
}

TEST_CASE("EndGroup without BeginGroup is ignored", "[commands][group]") {
    cgtest::EngineFixture fixture;
    ICommandStack& stack = *fixture.Scene().GetCommandStack();

    stack.EndGroup();
    stack.EndGroup();
    CHECK(stack.GetUndoCount() == 0);
    SUCCEED();
}

TEST_CASE("capacity trims the oldest entries", "[commands]") {
    cgtest::EngineFixture fixture;
    ICommandStack& stack = *fixture.Scene().GetCommandStack();
    Ledger ledger;

    stack.SetCapacity(3);
    for (int i = 0; i < 6; ++i) {
        stack.Push(new AddCommand(ledger, 1));
    }

    CHECK(stack.GetUndoCount() == 3);
    // The three that fell off the bottom were freed, not dropped on the floor.
    CHECK(ledger.released == 3);
    CHECK(ledger.value == 6);
}

TEST_CASE("Clear releases both branches", "[commands]") {
    cgtest::EngineFixture fixture;
    ICommandStack& stack = *fixture.Scene().GetCommandStack();
    Ledger ledger;

    stack.Push(new AddCommand(ledger, 1));
    stack.Push(new AddCommand(ledger, 2));
    stack.Undo();
    REQUIRE(stack.GetUndoCount() == 1);
    REQUIRE(stack.GetRedoCount() == 1);

    stack.Clear();
    CHECK(stack.GetUndoCount() == 0);
    CHECK(stack.GetRedoCount() == 0);
    CHECK(ledger.released == 2);
}

TEST_CASE("engine teardown releases commands still on the stack", "[commands][lifetime]") {
    Ledger ledger;
    {
        cgtest::EngineFixture fixture;
        ICommandStack& stack = *fixture.Scene().GetCommandStack();
        stack.Push(new AddCommand(ledger, 1));
        stack.BeginGroup("Never closed");
        stack.Push(new AddCommand(ledger, 2));
        // Deliberately no EndGroup: a host that crashes out mid-gesture must
        // still not leak the commands it already handed over.
    }
    CHECK(ledger.released == 2);
}

TEST_CASE("commands can edit the scene through the interface they are given",
          "[commands][scene]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    struct CreateGroupCommand final : public ICommand {
        explicit CreateGroupCommand(EntityId& out) : out_(out) {}
        void Release() override { delete this; }
        CgResult Execute(IScene* s) override {
            out_ = s->CreateGroup("FromCommand", kInvalidEntity);
            return IsValid(out_) ? CgResult::Ok : CgResult::Unknown;
        }
        CgResult Undo(IScene* s) override { return s->DestroyEntity(out_); }
        const char* GetName() const override { return "Create Group"; }

    private:
        ~CreateGroupCommand() override = default;
        EntityId& out_;
    };

    EntityId created{};
    REQUIRE(CgSucceeded(scene.GetCommandStack()->Push(new CreateGroupCommand(created))));
    CHECK(scene.GetEntityCount() == 1);
    CHECK(scene.Exists(created));

    REQUIRE(CgSucceeded(scene.GetCommandStack()->Undo()));
    CHECK(scene.GetEntityCount() == 0);
}
