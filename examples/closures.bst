#!/usr/bin/env beast
# Closures as objects: a bank account with private balance.
fn account(balance) {
    return {
        deposit: fn(amount) { balance += amount; return balance },
        withdraw: fn(amount) { balance -= amount; return balance },
        balance: fn() { return balance },
    }
}
let acc = account(100)
let dep = acc.deposit
let wd = acc.withdraw
let bal = acc.balance
dep(50)
wd(30)
print("Balance: " + str(bal()))    # 120
