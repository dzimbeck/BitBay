[![Codacy Badge](https://api.codacy.com/project/badge/Grade/1bac5bbdf2f64cfeb67092bef3e50d6f)](https://www.codacy.com/app/yshurik/bitbay-core?utm_source=github.com&utm_medium=referral&utm_content=bitbaymarket/bitbay-core&utm_campaign=badger)
[![Build Status](https://travis-ci.org/bitbaymarket/bitbay-core.svg?branch=master)](https://travis-ci.org/bitbaymarket/bitbay-core)
[![Build status](https://ci.appveyor.com/api/projects/status/qdy7pilwdtxehqhw?svg=true)](https://ci.appveyor.com/project/yshurik/bitbay-core)
[![Open Source Love](https://badges.frapsoft.com/os/mit/mit.svg?v=102)](https://github.com/bitbaymarket/bitbay-core/blob/master/COPYING)


BitBay development tree

BitBay is a PoS-based cryptocurrency.

BitBay
===========================

BitBay is the world's first fully-functional decentralized marketplace since 2014 even before Ethereum. Using innovative technology, BitBay enables you to buy and sell goods and services securely and anonymously. The marketplace is built free and open source. The markets are based on Bitmessage and use double deposit escrow. This is a two party escrow that eliminates the middle man and removes the incentive for default due to both parties having a deposit in the joint account. They either win together or they lose together, without being able to profit off of fraud. This makes theft and fraud impossible in these contracts for the first time in history without the need of an escrow agent. 

BitBay also is the only coin in the world with a truly variable supply. Since other proposals of variable supply see no shift in equity they have zero impact on controlling the economy. BitBay is different because users hold equity over what is moved into reserve(sort of like a negative interest rate moving a users funds into a savings account). Therefore during inflation only users who hold reserve will see their coins released into circulation. If the price drops, deflation can protect the economy by moving some of the funds of users back into reserve. BitBay has been able to prove that even under extreme duress and volatility with little buy support it was able to hold it's price and subsequently, any price the community wanted it to hold.

The target price of this dynamic supply(also called dynamic peg) is determined by the users of the coin. So unlike a stablecoin, collateral is not required and BitBay can increase in price indefinitely or it can hold a stable price. This is purely determined by the stakers/users who also protect the network. So the decision to increase or decrease supply is purely decentralized and based on user consensus. Typically users choose an algorithm to vote on supply for them to target peaks.

This variable supply is accomplished because all user balances are arrays and liquid and reserve balances are determined based on the supply index of the entire economy. When moving liquid funds, users pull coins from each array column based on it's ratio. When moving reserve funds the coins that are less than the supply index are moved and subject to a one month time delay similar to a bond or long term savings. Therefore a user has two balances, liquid and reserve. BitBay also supplies code for central exchanges that wish to implement this peg so that they can handle the arrays on their orderbooks. There was also a mock exchange set up for over a year to demonstrate the process. Although ultimately, BitBay is best suited for decentralized exchanges as we consider that safer and better suited for the technical challenge of exchange implementation.

Development process
===========================

Developers work in their own trees, then submit pull requests when
they think their feature or bug fix is ready.

The patch will be accepted if there is broad consensus that it is a
good thing.  Developers should expect to rework and resubmit patches
if they don't match the project's coding conventions (see coding.txt)
or are controversial.

The master branch is regularly built and tested, but is not guaranteed
to be completely stable. Tags are regularly created to indicate new
stable release versions of BitBay.

Feature branches are created when there are major new features being
worked on by several people.

From time to time a pull request will become outdated. If this occurs, and
the pull is no longer automatically mergeable; a comment on the pull will
be used to issue a warning of closure. The pull will be closed 15 days
after the warning if action is not taken by the author. Pull requests closed
in this manner will have their corresponding issue labeled 'stagnant'.

Issues with no commits will be given a similar warning, and closed after
15 days from their last activity. Issues closed in this manner will be
labeled 'stale'.
