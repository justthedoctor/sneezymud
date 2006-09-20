#ifndef __SHOPOWNED_H
#define __SHOPOWNED_H

class TShopOwned {
  int shop_nr;
  TMonster *keeper;
  TBeing *ch;
  bool owned;
  int access;

 public:
  bool isOwned();
  bool hasAccess(int);
  int getPurchasePrice(int, int);
  int getCorpID();

  void doBuyTransaction(int, const sstring &, const sstring &, TObj *obj=NULL);
  double getExpenseRatio();
  bool doExpenses(int, TObj *);


  void setDividend(sstring);
  double getDividend();
  int doDividend(int, const sstring &);

  void setReserve(sstring);
  int getMinReserve();
  int getMaxReserve();
  int doReserve();
  int chargeTax(int, const sstring &, TObj *);

  // repair specific
  double getQuality();
  void setQuality(sstring);
  double getSpeed();
  void setSpeed(sstring);

  int getMaxNum(const TObj *);
  void showInfo();
  int setRates(sstring);
  int buyShop(sstring);
  int sellShop();
  int giveMoney(sstring);
  int setAccess(sstring);
  int doLogs(sstring);
  int setString(sstring);

  // accounting stuff
  void journalize(sstring, sstring, sstring, int, int, int);


  TShopOwned(int, TMonster *, TBeing *);
  ~TShopOwned();
};


#endif
