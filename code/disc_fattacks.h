//////////////////////////////////////////////////////////////////////////
//
// SneezyMUD - All rights reserved, SneezyMUD Coding Team
//
// $Log: disc_fattacks.h,v $
// Revision 1.1  1999/09/12 17:24:04  peel
// Initial revision
//
//
//////////////////////////////////////////////////////////////////////////


#ifndef __DISC_FOCUSED_ATTACKS_H
#define __DISC_FOCUSED_ATTACKS_H 


class CDFAttacks : public CDiscipline
{
public:
    CSkill skQuiveringPalm;
    CSkill skCriticalHitting;

    CDFAttacks();
    CDFAttacks(const CDFAttacks &a);
    CDFAttacks & operator=(const CDFAttacks &a);
    virtual ~CDFAttacks();
    virtual CDFAttacks * cloneMe();

private:
};

#endif







