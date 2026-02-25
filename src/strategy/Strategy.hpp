#pragma once
#include "../core/Types.hpp"
#include "../portfolio/Portfolio.hpp"
#include <string>
#include <vector>

// ── Action ─────────────────────────────────────────────────────────────────────
// What a strategy wants the engine to do on its behalf.
struct Action {
    enum class Type { SUBMIT, CANCEL };
    Type        type       = Type::SUBMIT;
    Order       order      = {};          // used when type == SUBMIT
    std::string cancel_id  = {};          // used when type == CANCEL
    std::string cancel_sym = {};          // symbol of the order to cancel
};

// ── Strategy ───────────────────────────────────────────────────────────────────
// All strategies implement this interface.  The engine calls them with events
// and routes the resulting Actions through risk checks → exchange → fills.
class Strategy {
public:
    virtual ~Strategy() = default;

    // Unique identifier (used to tag orders and route fills)
    virtual const std::string& id() const = 0;

    // Called on each market quote tick.  Returns a (possibly empty) list of
    // actions the strategy wants to take.
    virtual std::vector<Action> onQuote(const Quote& q, const Portfolio& pf) = 0;

    // Called when one of our orders is filled.
    virtual void onFill(const Fill& f) = 0;

    // Called periodically (timer events).  Default: no-op.
    virtual std::vector<Action> onTimer(Timestamp /*ts*/, const Portfolio& /*pf*/) {
        return {};
    }

protected:
    // Helper: build a submit action with a limit order
    static Action submitLimit(const std::string& strat_id,
                               const std::string& order_id,
                               const std::string& symbol,
                               Side side, double price, int qty,
                               Timestamp ts) {
        Order o;
        o.id       = order_id;
        o.symbol   = symbol;
        o.strat_id = strat_id;
        o.side     = side;
        o.type     = OrdType::LIMIT;
        o.price    = price;
        o.qty      = qty;
        o.ts       = ts;
        return Action{ Action::Type::SUBMIT, o };
    }

    // Helper: build a submit action with a market order
    static Action submitMarket(const std::string& strat_id,
                                const std::string& order_id,
                                const std::string& symbol,
                                Side side, int qty,
                                Timestamp ts) {
        Order o;
        o.id       = order_id;
        o.symbol   = symbol;
        o.strat_id = strat_id;
        o.side     = side;
        o.type     = OrdType::MARKET;
        o.qty      = qty;
        o.ts       = ts;
        return Action{ Action::Type::SUBMIT, o };
    }

    // Helper: build a cancel action
    static Action cancelOrder(const std::string& symbol,
                               const std::string& order_id) {
        Action a;
        a.type       = Action::Type::CANCEL;
        a.cancel_id  = order_id;
        a.cancel_sym = symbol;
        return a;
    }
};
