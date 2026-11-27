// Mini Stock Exchange - a small C++17 project that demonstrates core OOP concepts.
//
// Concept map (search for these tags in the code):
//   [ENCAPSULATION]  private data + public getters
//   [ABSTRACTION]    abstract classes with pure virtual functions
//   [INHERITANCE]    LimitOrder/MarketOrder/StopLossOrder -> Order,
//                    RetailTrader/InstitutionalTrader -> Trader
//   [POLYMORPHISM]   virtual functions called through base-class pointers (runtime),
//                    function and operator overloading, templates (compile time)
//   [CONSTRUCTORS]   constructor overloading, initializer lists, virtual destructors
//   [STATIC]         static member shared by all objects
//   [FRIEND]         friend function for operator<<
//   [COMPOSITION]    Exchange "has an" OrderBook, "has" Traders, "builds" TraderReports
//   [EXCEPTIONS]     custom exception class
//   [DYNAMIC_CAST]   dynamic_pointer_cast to identify order subtypes at runtime
//   [NESTED_STRUCT]  Entry inside TraderReport groups related data

#include <algorithm>
#include <ctime>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

enum class Side { BUY, SELL };

inline std::string toString(Side s) { return s == Side::BUY ? "BUY" : "SELL"; }

// helper: format a time_t as "DD-Mon-YYYY"
inline std::string formatDate(std::time_t t) {
    char buf[20];
    std::strftime(buf, sizeof(buf), "%d-%b-%Y", std::localtime(&t));
    return buf;
}

// [EXCEPTIONS] [INHERITANCE] custom exception derived from a standard one
class InvalidOrderException : public std::runtime_error {
public:
    explicit InvalidOrderException(const std::string& msg) : std::runtime_error(msg) {}
};

// ======================= Order hierarchy =======================

// [ABSTRACTION] Order is abstract: it has pure virtual functions, so it cannot be instantiated.
class Order {
private:                                   // [ENCAPSULATION] data is hidden from outside code
    static int nextId;                     // [STATIC] one counter shared by every order
    int id_;
    std::string trader_;
    Side side_;
    int quantity_;

public:
    // [CONSTRUCTORS] initializer list sets members before the body runs
    Order(const std::string& trader, Side side, int qty)
        : id_(nextId++), trader_(trader), side_(side), quantity_(qty) {
        if (qty <= 0) throw InvalidOrderException("Quantity must be positive");
    }
    virtual ~Order() = default;            // [CONSTRUCTORS] virtual destructor for safe deletion via base pointer

    // [ENCAPSULATION] controlled access through const getters
    int id() const { return id_; }
    const std::string& trader() const { return trader_; }
    Side side() const { return side_; }
    int quantity() const { return quantity_; }
    bool isFilled() const { return quantity_ == 0; }
    void fill(int qty) { quantity_ -= qty; }

    // [ABSTRACTION] derived classes must provide these
    virtual bool isMarket() const = 0;
    virtual double price() const = 0;
    virtual std::string type() const = 0;
    virtual std::string info() const { return ""; }   // extra display info for subtypes

    // [FRIEND] non-member function allowed to read private members
    friend std::ostream& operator<<(std::ostream& os, const Order& o);
};
int Order::nextId = 1;

// [POLYMORPHISM] operator overloading; o.type() and o.info() are resolved at runtime (virtual call)
std::ostream& operator<<(std::ostream& os, const Order& o) {
    os << "#" << o.id_ << " " << o.type() << " " << toString(o.side_) << " " << o.quantity_
       << " by " << o.trader_;
    if (!o.isMarket()) os << " @ " << std::fixed << std::setprecision(2) << o.price();
    std::string extra = o.info();
    if (!extra.empty()) os << extra;
    return os;
}

// [INHERITANCE] LimitOrder "is an" Order
class LimitOrder : public Order {
private:
    double price_;

public:
    LimitOrder(const std::string& trader, Side side, int qty, double price)
        : Order(trader, side, qty), price_(price) {
        if (price <= 0) throw InvalidOrderException("Price must be positive");
    }
    bool isMarket() const override { return false; }   // [POLYMORPHISM] overriding
    double price() const override { return price_; }
    std::string type() const override { return "LIMIT"; }
};

// [INHERITANCE] MarketOrder "is an" Order: executes at the best available price
class MarketOrder : public Order {
public:
    MarketOrder(const std::string& trader, Side side, int qty) : Order(trader, side, qty) {}
    bool isMarket() const override { return true; }
    double price() const override { return 0.0; }
    std::string type() const override { return "MARKET"; }
};

// [INHERITANCE] StopLossOrder "is an" Order: stays pending until the market hits a trigger price,
// then activates as a limit order.  Like Zerodha's SL order.
class StopLossOrder : public Order {
private:
    double triggerPrice_;
    double limitPrice_;
    bool triggered_ = false;

public:
    StopLossOrder(const std::string& trader, Side side, int qty,
                  double triggerPrice, double limitPrice)
        : Order(trader, side, qty), triggerPrice_(triggerPrice), limitPrice_(limitPrice) {
        if (triggerPrice <= 0) throw InvalidOrderException("Trigger price must be positive");
        if (limitPrice <= 0)  throw InvalidOrderException("Limit price must be positive");
    }

    bool isMarket() const override { return false; }
    double price()    const override { return limitPrice_; }
    std::string type() const override { return triggered_ ? "SL-ACTIVE" : "SL-PENDING"; }

    double triggerPrice() const { return triggerPrice_; }
    bool   isTriggered()  const { return triggered_; }
    void   activate()           { triggered_ = true; }

    // [POLYMORPHISM] sell SL triggers when price drops; buy SL triggers when price rises
    bool shouldTrigger(double lastPrice) const {
        if (triggered_) return false;
        return (side() == Side::SELL) ? lastPrice <= triggerPrice_
                                     : lastPrice >= triggerPrice_;
    }

    // [POLYMORPHISM] extra display info shows the trigger price
    std::string info() const override {
        std::ostringstream oss;
        oss << " (trigger " << std::fixed << std::setprecision(2) << triggerPrice_ << ")";
        return oss.str();
    }
};

// ======================= Trade =======================

class Trade {
private:
    int buyOrderId_;
    int sellOrderId_;
    std::string buyer_;
    std::string seller_;
    double price_;
    int quantity_;
    std::time_t timestamp_;

public:
    Trade() : buyOrderId_(0), sellOrderId_(0), price_(0.0), quantity_(0),
              timestamp_(std::time(nullptr)) {}                                    // [CONSTRUCTORS] default
    Trade(int buyId, int sellId, const std::string& buyer, const std::string& seller,
          double price, int qty, std::time_t ts = std::time(nullptr))              // [CONSTRUCTORS] parameterized
        : buyOrderId_(buyId), sellOrderId_(sellId), buyer_(buyer), seller_(seller),
          price_(price), quantity_(qty), timestamp_(ts) {}

    // [ENCAPSULATION] const getters
    int buyOrderId()            const { return buyOrderId_; }
    int sellOrderId()           const { return sellOrderId_; }
    const std::string& buyer()  const { return buyer_; }
    const std::string& seller() const { return seller_; }
    double price()              const { return price_; }
    int quantity()              const { return quantity_; }
    std::time_t timestamp()     const { return timestamp_; }

    friend std::ostream& operator<<(std::ostream& os, const Trade& t) {
        return os << "TRADE buy#" << t.buyOrderId_ << " sell#" << t.sellOrderId_
                  << " " << t.quantity_ << " @ " << std::fixed << std::setprecision(2)
                  << t.price_ << "  [" << formatDate(t.timestamp_) << "]";
    }
};

// ======================= Trader Report =======================

// [ENCAPSULATION] [COMPOSITION] aggregates a trader's buy/sell history and computes P&L
class TraderReport {
private:
    std::string name_;
    int months_;

    // [NESTED_STRUCT] groups related fields for a single trade entry
    struct Entry {
        int orderId;
        double price;
        int quantity;
        std::time_t timestamp;
        double value() const { return price * quantity; }
    };

    std::vector<Entry> buys_;
    std::vector<Entry> sells_;

public:
    TraderReport(const std::string& name, int months) : name_(name), months_(months) {}

    void addBuy(int orderId, double price, int qty, std::time_t ts) {
        buys_.push_back({orderId, price, qty, ts});
    }
    void addSell(int orderId, double price, int qty, std::time_t ts) {
        sells_.push_back({orderId, price, qty, ts});
    }

    bool empty() const { return buys_.empty() && sells_.empty(); }

    int totalBought() const {
        int t = 0; for (const auto& e : buys_) t += e.quantity; return t;
    }
    double totalBuyCost() const {
        double t = 0; for (const auto& e : buys_) t += e.value(); return t;
    }
    int totalSold() const {
        int t = 0; for (const auto& e : sells_) t += e.quantity; return t;
    }
    double totalSellRevenue() const {
        double t = 0; for (const auto& e : sells_) t += e.value(); return t;
    }
    double avgBuyPrice()  const { return totalBought() > 0 ? totalBuyCost() / totalBought() : 0.0; }
    double avgSellPrice() const { return totalSold() > 0 ? totalSellRevenue() / totalSold() : 0.0; }
    int    netPosition()  const { return totalBought() - totalSold(); }

    // realized P&L using average-cost method (same as Zerodha)
    double realizedPnL() const {
        int matched = std::min(totalBought(), totalSold());
        if (matched == 0) return 0.0;
        return matched * (avgSellPrice() - avgBuyPrice());
    }

    // [FRIEND] formatted report output
    friend std::ostream& operator<<(std::ostream& os, const TraderReport& r);
};

std::ostream& operator<<(std::ostream& os, const TraderReport& r) {
    std::string period = r.months_ > 0 ? "last " + std::to_string(r.months_) + " month(s)"
                                       : "all time";
    os << "\n====== Trade Report: " << r.name_ << " (" << period << ") ======\n";

    if (r.empty()) {
        os << "  No trades found.\n";
        os << "================================================\n";
        return os;
    }

    os << std::fixed << std::setprecision(2);

    if (!r.buys_.empty()) {
        os << "PURCHASES\n";
        for (const auto& e : r.buys_)
            os << "  order #" << e.orderId << "  " << e.quantity << " shares @ "
               << e.price << " = " << e.value() << "  [" << formatDate(e.timestamp) << "]\n";
        os << "  Total: " << r.totalBought() << " shares | Cost: " << r.totalBuyCost()
           << " | Avg price: " << r.avgBuyPrice() << "\n\n";
    }

    if (!r.sells_.empty()) {
        os << "SALES\n";
        for (const auto& e : r.sells_)
            os << "  order #" << e.orderId << "  " << e.quantity << " shares @ "
               << e.price << " = " << e.value() << "  [" << formatDate(e.timestamp) << "]\n";
        os << "  Total: " << r.totalSold() << " shares | Revenue: " << r.totalSellRevenue()
           << " | Avg price: " << r.avgSellPrice() << "\n\n";
    }

    os << "Net position: ";
    int net = r.netPosition();
    if (net > 0)       os << "+" << net << " shares (long)";
    else if (net < 0)  os << net << " shares (short)";
    else               os << "0 (flat)";

    os << "\nRealized P&L: ";
    double pnl = r.realizedPnL();
    if (pnl >= 0) os << "+";
    os << pnl;
    int matched = std::min(r.totalBought(), r.totalSold());
    if (matched > 0) os << "  (on " << matched << " matched shares, avg-cost method)";

    os << "\n================================================\n";
    return os;
}

// ======================= Trader hierarchy =======================

// [ABSTRACTION] abstract base class for market participants
class Trader {
protected:                                 // visible to derived classes only
    std::string name_;

public:
    explicit Trader(std::string name) : name_(std::move(name)) {}
    virtual ~Trader() = default;
    const std::string& name() const { return name_; }
    virtual int maxOrderSize() const = 0;
    virtual std::string category() const = 0;
};

class RetailTrader : public Trader {
public:
    explicit RetailTrader(const std::string& name) : Trader(name) {}
    int maxOrderSize() const override { return 100; }
    std::string category() const override { return "Retail"; }
};

class InstitutionalTrader : public Trader {
public:
    explicit InstitutionalTrader(const std::string& name) : Trader(name) {}
    int maxOrderSize() const override { return 10000; }
    std::string category() const override { return "Institutional"; }
};

// ======================= Order book =======================

class OrderBook {
private:
    // buy side: highest price first; sell side: lowest price first. Each price level is a FIFO queue.
    std::map<double, std::deque<std::shared_ptr<Order>>, std::greater<double>> bids_;
    std::map<double, std::deque<std::shared_ptr<Order>>> asks_;
    std::vector<Trade> trades_;
    std::vector<std::shared_ptr<StopLossOrder>> stopOrders_;   // pending stop-loss orders

    // [POLYMORPHISM] compile-time (template): the same matching code works for both sides of the book
    template <typename Book>
    void match(const std::shared_ptr<Order>& incoming, Book& opposite) {
        while (!incoming->isFilled() && !opposite.empty()) {
            auto level = opposite.begin();                 // best price on the opposite side
            double levelPrice = level->first;
            if (!incoming->isMarket()) {
                bool crosses = (incoming->side() == Side::BUY) ? incoming->price() >= levelPrice
                                                               : incoming->price() <= levelPrice;
                if (!crosses) break;                       // prices do not overlap: stop matching
            }
            auto& queue = level->second;
            std::shared_ptr<Order> resting = queue.front();   // oldest order at this price (time priority)
            int qty = std::min(incoming->quantity(), resting->quantity());
            incoming->fill(qty);
            resting->fill(qty);

            const Order& buy = (incoming->side() == Side::BUY) ? *incoming : *resting;
            const Order& sell = (incoming->side() == Side::SELL) ? *incoming : *resting;
            trades_.emplace_back(buy.id(), sell.id(), buy.trader(), sell.trader(),
                                 levelPrice, qty);

            if (resting->isFilled()) queue.pop_front();
            if (queue.empty()) opposite.erase(level);
        }
    }

    // [DYNAMIC_CAST] after every trade, check whether any pending stop-loss should activate
    void checkStopOrders() {
        if (trades_.empty() || stopOrders_.empty()) return;

        bool activated;
        do {
            activated = false;
            double lastPrice = trades_.back().price();
            auto it = stopOrders_.begin();
            while (it != stopOrders_.end()) {
                if ((*it)->shouldTrigger(lastPrice)) {
                    auto sl = *it;
                    sl->activate();
                    std::cout << "  >>> SL triggered: " << *sl << "\n";
                    it = stopOrders_.erase(it);

                    // submit the activated order into the book as a regular limit order
                    std::shared_ptr<Order> order = sl;
                    if (order->side() == Side::BUY)  match(order, asks_);
                    else                             match(order, bids_);

                    if (!order->isFilled()) {
                        if (order->side() == Side::BUY) bids_[order->price()].push_back(order);
                        else                            asks_[order->price()].push_back(order);
                    }
                    activated = true;   // new trades may trigger more stop-losses (cascade)
                } else {
                    ++it;
                }
            }
        } while (activated);
    }

public:
    void submit(const std::shared_ptr<Order>& order) {
        // [DYNAMIC_CAST] detect stop-loss orders at runtime and park them separately
        auto slOrder = std::dynamic_pointer_cast<StopLossOrder>(order);
        if (slOrder && !slOrder->isTriggered()) {
            stopOrders_.push_back(slOrder);
            return;
        }

        if (order->side() == Side::BUY) match(order, asks_);
        else match(order, bids_);

        if (!order->isFilled()) {
            if (order->isMarket()) {
                std::cout << "  (market order #" << order->id() << " partly unfilled: " << order->quantity()
                          << " left, no liquidity)\n";
            } else if (order->side() == Side::BUY) {
                bids_[order->price()].push_back(order);    // rest the remainder in the book
            } else {
                asks_[order->price()].push_back(order);
            }
        }

        checkStopOrders();
    }

    const std::vector<Trade>& trades() const { return trades_; }

    void print() const {
        std::cout << "---- ORDER BOOK ----\n";
        std::cout << "SELLS (lowest first)\n";
        for (const auto& [price, queue] : asks_)
            for (const auto& o : queue) std::cout << "  " << *o << "\n";   // [POLYMORPHISM] prints correct subtype
        std::cout << "BUYS (highest first)\n";
        for (const auto& [price, queue] : bids_)
            for (const auto& o : queue) std::cout << "  " << *o << "\n";
        if (!stopOrders_.empty()) {
            std::cout << "STOP-LOSS (pending)\n";
            for (const auto& o : stopOrders_) std::cout << "  " << *o << "\n";
        }
        std::cout << "--------------------\n";
    }
};

// [POLYMORPHISM] template function: works for any type that supports operator<<
template <typename T>
void printAll(const std::string& title, const std::vector<T>& items) {
    std::cout << title << " (" << items.size() << ")\n";
    for (const auto& item : items) std::cout << "  " << item << "\n";
}

// ======================= Exchange =======================

// [COMPOSITION] Exchange owns an OrderBook and a collection of traders
class Exchange {
private:
    OrderBook book_;
    std::map<std::string, std::unique_ptr<Trader>> traders_;

    void validate(const std::string& traderName, int qty) const {
        auto it = traders_.find(traderName);
        if (it == traders_.end()) throw InvalidOrderException("Unknown trader: " + traderName);
        if (qty > it->second->maxOrderSize())
            throw InvalidOrderException(traderName + " (" + it->second->category() + ") exceeds max order size " +
                                        std::to_string(it->second->maxOrderSize()));
    }

public:
    void registerTrader(std::unique_ptr<Trader> trader) {
        std::string name = trader->name();
        traders_[name] = std::move(trader);
    }

    // [POLYMORPHISM] function overloading: same name, different parameters
    void placeOrder(const std::string& trader, Side side, int qty, double price) {   // limit order
        validate(trader, qty);
        auto order = std::make_shared<LimitOrder>(trader, side, qty, price);
        std::cout << "Placed  " << *order << "\n";
        book_.submit(order);
    }
    void placeOrder(const std::string& trader, Side side, int qty) {                 // market order
        validate(trader, qty);
        auto order = std::make_shared<MarketOrder>(trader, side, qty);
        std::cout << "Placed  " << *order << "\n";
        book_.submit(order);
    }
    void placeOrder(const std::string& trader, Side side, int qty,                   // stop-loss order
                    double triggerPrice, double limitPrice) {
        validate(trader, qty);
        auto order = std::make_shared<StopLossOrder>(trader, side, qty, triggerPrice, limitPrice);
        std::cout << "Placed  " << *order << "\n";
        book_.submit(order);
    }

    // [COMPOSITION] builds a TraderReport by scanning the trade history
    void traderReport(const std::string& name, int months = 0) const {
        auto it = traders_.find(name);
        if (it == traders_.end()) {
            std::cout << "Unknown trader: " << name << "\n";
            return;
        }

        std::time_t now = std::time(nullptr);
        std::time_t cutoff = (months > 0)
            ? now - static_cast<std::time_t>(months) * 30 * 24 * 3600
            : 0;

        TraderReport report(name, months);
        for (const auto& trade : book_.trades()) {
            if (trade.timestamp() < cutoff) continue;
            if (trade.buyer() == name)
                report.addBuy(trade.buyOrderId(), trade.price(), trade.quantity(), trade.timestamp());
            if (trade.seller() == name)
                report.addSell(trade.sellOrderId(), trade.price(), trade.quantity(), trade.timestamp());
        }
        std::cout << report;
    }

    void listTraders() const {
        std::cout << "Registered traders\n";
        for (const auto& [name, trader] : traders_)                                  // [POLYMORPHISM] runtime dispatch
            std::cout << "  " << name << " - " << trader->category() << " (max " << trader->maxOrderSize() << ")\n";
    }

    const OrderBook& book() const { return book_; }
};

// ======================= Demo =======================

int main() {
    Exchange exchange;
    exchange.registerTrader(std::make_unique<RetailTrader>("Asha"));
    exchange.registerTrader(std::make_unique<RetailTrader>("Ravi"));
    exchange.registerTrader(std::make_unique<InstitutionalTrader>("AlphaFund"));
    exchange.listTraders();
    std::cout << "\n";

    // Build up resting limit orders
    exchange.placeOrder("Asha",      Side::SELL, 50, 101.00);
    exchange.placeOrder("Ravi",      Side::SELL, 30, 102.00);
    exchange.placeOrder("AlphaFund", Side::BUY,  40,  99.00);

    // Asha protects her position with a stop-loss:
    //   "If price drops to 98, sell 20 shares at limit 97.50"
    exchange.placeOrder("Asha", Side::SELL, 20, 98.00, 97.50);
    exchange.book().print();

    // A buy that crosses the spread -> trades at 101 and 102 (above trigger, SL stays pending)
    std::cout << "\n";
    exchange.placeOrder("AlphaFund", Side::BUY, 70, 102.00);
    exchange.book().print();

    // AlphaFund adds another bid at 98
    exchange.placeOrder("AlphaFund", Side::BUY, 30, 98.00);

    // Ravi sells aggressively -> trades at 99 then 98 -> triggers Asha's stop-loss!
    std::cout << "\n--- Ravi sells aggressively, price drops ---\n";
    exchange.placeOrder("Ravi", Side::SELL, 60, 97.00);
    exchange.book().print();

    // A market order sweeps the remaining liquidity from the triggered SL
    std::cout << "\n";
    exchange.placeOrder("AlphaFund", Side::BUY, 10);
    exchange.book().print();

    // AlphaFund takes profit by selling 30 shares to Ravi at 103.00
    std::cout << "\n";
    exchange.placeOrder("Ravi", Side::BUY, 30, 103.00);
    exchange.placeOrder("AlphaFund", Side::SELL, 30, 103.00);

    // Error handling: exceeds the retail limit, and an invalid price
    std::cout << "\n";
    try {
        exchange.placeOrder("Asha", Side::BUY, 500, 100.00);
    } catch (const InvalidOrderException& e) {
        std::cout << "Rejected: " << e.what() << "\n";
    }
    try {
        exchange.placeOrder("Ravi", Side::SELL, 10, -5.0);
    } catch (const InvalidOrderException& e) {
        std::cout << "Rejected: " << e.what() << "\n";
    }

    std::cout << "\n";
    printAll("Trades executed", exchange.book().trades());

    // Trader reports: view each participant's buys, sells, and realized P&L
    exchange.traderReport("AlphaFund", 1);   // last 1 month
    exchange.traderReport("Asha", 1);
    exchange.traderReport("Ravi", 1);

    return 0;
}
