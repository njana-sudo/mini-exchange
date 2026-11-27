// Mini Stock Exchange - a small C++17 project that demonstrates core OOP concepts.
//
// Concept map (search for these tags in the code):
//   [ENCAPSULATION]  private data + public getters
//   [ABSTRACTION]    abstract classes with pure virtual functions
//   [INHERITANCE]    LimitOrder/MarketOrder -> Order, RetailTrader/InstitutionalTrader -> Trader
//   [POLYMORPHISM]   virtual functions called through base-class pointers (runtime),
//                    function and operator overloading, templates (compile time)
//   [CONSTRUCTORS]   constructor overloading, initializer lists, virtual destructors
//   [STATIC]         static member shared by all objects
//   [FRIEND]         friend function for operator<<
//   [COMPOSITION]    Exchange "has an" OrderBook and "has" Traders
//   [EXCEPTIONS]     custom exception class

#include <algorithm>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

enum class Side { BUY, SELL };

inline std::string toString(Side s) { return s == Side::BUY ? "BUY" : "SELL"; }

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

    // [FRIEND] non-member function allowed to read private members
    friend std::ostream& operator<<(std::ostream& os, const Order& o);
};
int Order::nextId = 1;

// [POLYMORPHISM] operator overloading; o.type() is resolved at runtime (virtual call)
std::ostream& operator<<(std::ostream& os, const Order& o) {
    os << "#" << o.id_ << " " << o.type() << " " << toString(o.side_) << " " << o.quantity_
       << " by " << o.trader_;
    if (!o.isMarket()) os << " @ " << std::fixed << std::setprecision(2) << o.price();
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

// ======================= Trade =======================

class Trade {
private:
    int buyOrderId_;
    int sellOrderId_;
    double price_;
    int quantity_;

public:
    Trade() : buyOrderId_(0), sellOrderId_(0), price_(0.0), quantity_(0) {}   // [CONSTRUCTORS] default
    Trade(int buyId, int sellId, double price, int qty)                        // [CONSTRUCTORS] parameterized
        : buyOrderId_(buyId), sellOrderId_(sellId), price_(price), quantity_(qty) {}

    friend std::ostream& operator<<(std::ostream& os, const Trade& t) {
        return os << "TRADE buy#" << t.buyOrderId_ << " sell#" << t.sellOrderId_ << " " << t.quantity_
                  << " @ " << std::fixed << std::setprecision(2) << t.price_;
    }
};

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
            trades_.emplace_back(buy.id(), sell.id(), levelPrice, qty);

            if (resting->isFilled()) queue.pop_front();
            if (queue.empty()) opposite.erase(level);
        }
    }

public:
    void submit(const std::shared_ptr<Order>& order) {
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

    // Build up resting orders
    exchange.placeOrder("Asha", Side::SELL, 50, 101.00);
    exchange.placeOrder("Ravi", Side::SELL, 30, 102.00);
    exchange.placeOrder("AlphaFund", Side::BUY, 40, 99.00);
    exchange.book().print();

    // A buy that crosses the spread -> trades happen
    std::cout << "\n";
    exchange.placeOrder("AlphaFund", Side::BUY, 70, 102.00);
    // A market order takes whatever is available
    exchange.placeOrder("Ravi", Side::BUY, 20);
    exchange.book().print();

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
    return 0;
}