//////////////////////////////////////////////////////////////////////////
//
//      SneezyMUD - All rights reserved, SneezyMUD Coding Team
//
//     "actions.cc" - All social functions and routines
//
//////////////////////////////////////////////////////////////////////////

#include "stdsneezy.h"
#include "obj_pool.h"
#include "obj_plant.h"
#include "disc_sorcery.h"
#include "liquids.h"

class socialMessg {
  public:
    bool hide;
    positionTypeT minPos;
  
    // No argument was supplied 
    const char *char_no_arg;
    const char *others_no_arg;
  
    // An argument was there, and a victim was found 
    const char *char_found;            
    const char *others_found;
    const char *vict_found;
  
    // An argument was there, but no victim was found 
    const char *not_found;
  
    // The victim turned out to be the character
    const char *char_auto;
    const char *others_auto;

    socialMessg() :
      hide(false),
      minPos(POSITION_DEAD),
      char_no_arg(NULL),
      others_no_arg(NULL),
      char_found(NULL),
      others_found(NULL),
      vict_found(NULL),
      not_found(NULL),
      char_auto(NULL),
      others_auto(NULL)
    {
    }
    socialMessg(const socialMessg &a) :
      hide(a.hide),
      minPos(a.minPos)
    {
      char_no_arg = mud_str_dup(a.char_no_arg);
      others_no_arg = mud_str_dup(a.others_no_arg);
      char_found = mud_str_dup(a.char_found);
      others_found = mud_str_dup(a.others_found);
      vict_found = mud_str_dup(a.vict_found);
      not_found = mud_str_dup(a.not_found);
      char_auto = mud_str_dup(a.char_auto);
      others_auto = mud_str_dup(a.others_auto);
    }
    ~socialMessg() {
      delete [] char_no_arg;
      delete [] others_no_arg;
      delete [] char_found;
      delete [] others_found;
      delete [] vict_found;
      delete [] not_found;
      delete [] char_auto;
      delete [] others_auto;
      char_no_arg = NULL;
      others_no_arg = NULL;
      char_found = NULL;
      others_found = NULL;
      vict_found = NULL;
      not_found = NULL;
      char_auto = NULL;
      others_auto = NULL;
    }
    socialMessg & operator = (const socialMessg &a) {
      if (this == &a) return *this;
      
      hide = a.hide;
      minPos = a.minPos;

      delete [] char_no_arg;
      delete [] others_no_arg;
      delete [] char_found;
      delete [] others_found;
      delete [] vict_found;
      delete [] not_found;
      delete [] char_auto;
      delete [] others_auto;

      char_no_arg = mud_str_dup(a.char_no_arg);
      others_no_arg = mud_str_dup(a.others_no_arg);
      char_found = mud_str_dup(a.char_found);
      others_found = mud_str_dup(a.others_found);
      vict_found = mud_str_dup(a.vict_found);
      not_found = mud_str_dup(a.not_found);
      char_auto = mud_str_dup(a.char_auto);
      others_auto = mud_str_dup(a.others_auto);

      return *this;
    }
};

char *fread_action(FILE *fl)
{
  char buf[MAX_STRING_LENGTH];

  for (;;) {
    fgets(buf, MAX_STRING_LENGTH, fl);

    if (feof(fl)) {
      vlogf(LOG_FILE, "Fread_action - unexpected EOF.");
      exit(0);
    }

    if (*buf == '#')
      return NULL;
    else {
      *(buf + strlen(buf) - 1) = '\0';
      return mud_str_dup(buf);
    }
  }
}

static map<int, socialMessg>soc_mess_list;

void bootSocialMessages(void)
{
  FILE *fl;
  int hide, min_pos;
  char buf[256];

  if (!(fl = fopen(SOCMESS_FILE, "r"))) {
    perror("bootSocialMessages");
    exit(0);
  }

  for (;;) {
    fscanf(fl, " %s ", buf);
    cmdTypeT tmp = searchForCommandNum(buf);

    if (tmp >= MAX_CMD_LIST)
      break;

    fscanf(fl, " %d ", &hide);
    fscanf(fl, " %d \n", &min_pos);

    socialMessg sm;
    sm.hide = hide;
    
    sm.minPos = mapFileToPos(min_pos);

    sm.char_no_arg = fread_action(fl);
    sm.others_no_arg = fread_action(fl);
    sm.char_found = fread_action(fl);

    if (!sm.char_found) {
      sm.others_found = NULL;
      sm.vict_found = NULL;
      sm.not_found = NULL;
      sm.char_auto = NULL;
      sm.others_auto = NULL;

      soc_mess_list[tmp] = sm;
      continue;
    }

    sm.others_found = fread_action(fl);
    sm.vict_found = fread_action(fl);
    sm.not_found = fread_action(fl);
    sm.char_auto = fread_action(fl);
    sm.others_auto = fread_action(fl);

    soc_mess_list[tmp] = sm;
  }
  fclose(fl);
}

// returns DELETE_THIS
int TBeing::doAction(const sstring & argument, cmdTypeT cmd)
{
  sstring buf;
  TBeing *vict;
  TMonster *tmp = NULL;
  TThing *t, *t2, *tvict=NULL;
  int rc;

  if (fight() || riding) {
    switch(cmd) {
// disallow  while fighting, allow for riding
// lower torso actions (dance, wiggle) prevented
      case CMD_BOP:
      case CMD_APPLAUD:
      case CMD_BEG:
      case CMD_TIP:
      case CMD_MOAN:
      case CMD_RAISE:
      case CMD_PET:
      case CMD_COMFORT:
      case CMD_YODEL:
      case CMD_GREET:
      case CMD_BOW:
      case CMD_CLAP:
      case CMD_SNAP:
      case CMD_PAT:
      case CMD_HUG:
      case CMD_POINT:
      case CMD_WAVE:
      case CMD_STRETCH:
      case CMD_FLIPOFF:
      case CMD_TWIDDLE:
      case CMD_LOVE:
      case CMD_SULK:
      case CMD_COMB:
      case CMD_FLEX:
      case CMD_BECKON:
      case CMD_SALUTE:
        if (fight())  {
          sendTo("You cannot perform that action while fighting!\n\r");
          return FALSE;
        }
        break;
// allowed while riding or fighting
// this should be list of facial expressions only, no hands allowed
      case CMD_AVERT:
      case CMD_WINK:
      case CMD_BOGGLE:
      case CMD_RAZZ:
      case CMD_WINCE:
      case CMD_BRANDISH:
      case CMD_GRUNT:
      case CMD_SCOLD:
      case CMD_BELITTLE:
      case CMD_CHORTLE:
      case CMD_APOLOGIZE:
      case CMD_WHIMPER:
      case CMD_SNEER:
      case CMD_MOO:
      case CMD_WHINE:
      case CMD_ACCUSE:
      case CMD_SNORE:
      case CMD_STEAM:
      case CMD_PEER:
      case CMD_PURR:
      case CMD_BLEED:
      case CMD_DAYDREAM:
      case CMD_HICCUP:
      case CMD_NOD:
      case CMD_FUME:
      case CMD_PUKE:
      case CMD_WHISTLE:
      case CMD_CRINGE:
      case CMD_THINK:
      case CMD_THANK:
      case CMD_ARCH:
      case CMD_SHRUG:
      case CMD_SIGH:
      case CMD_TAUNT:
      case CMD_GIGGLE:
      case CMD_CHEER:
      case CMD_GRUMBLE:
      case CMD_SNICKER:
      case CMD_CACKLE:
      case CMD_GASP:
      case CMD_GRIN:
      case CMD_SHAKE:  // shake head no, too bad this is also shake hand
      case CMD_GRIMACE:
      case CMD_SING:
      case CMD_SMILE:
      case CMD_HOWL:
      case CMD_STARE:
      case CMD_SNARL:
      case CMD_WOO:
      case CMD_CHUCKLE:
      case CMD_GROWL:
      case CMD_LAUGH:
      case CMD_CURSE:
      case CMD_CRY:
      case CMD_POUT:
      case CMD_SPIT:
      case CMD_SMIRK:
      case CMD_FROWN:
      case CMD_BEAM:
      case CMD_PONDER:
      case CMD_GLARE:
      case CMD_SCREAM:
      case CMD_AGREE:
      case CMD_YAWN:
      case CMD_DISAGREE:
      case CMD_SNORT:
      case CMD_ROAR:
      case CMD_ROLL:
      case CMD_GROAN:
      case CMD_BLUSH:
      case CMD_SHIVER:
      case CMD_DROOL:
      case CMD_COUGH:
      case CMD_SNIFF:
      case CMD_BURP:
      case CMD_FART:
      case CMD_SNEEZE:
      case CMD_HUM:
      case CMD_BLINK:
        break;
      default:
        // prevented due to riding/fighting
        sendTo("You currently cannot perform that action!\n\r");
        return FALSE;
    }
  }

  map<int, socialMessg>::const_iterator CT;
  CT = soc_mess_list.find(cmd);

  if (CT == soc_mess_list.end()) {
    sendTo("That action is not supported.\n\r");
    return FALSE;
  }

  socialMessg action = CT->second;

  if (action.char_found)
    buf=argument;

  if (roomp) {
    switch (cmd) {
      case CMD_YAWN:
        roomp->playsound(pickRandSound(SOUND_YAWN_1, SOUND_YAWN_4), SOUND_TYPE_SOCIAL);
        break;
      case CMD_GIGGLE:
        roomp->playsound(SOUND_GIGGLE, SOUND_TYPE_SOCIAL);
        break;
      case CMD_BURP:
        roomp->playsound(SOUND_BURP, SOUND_TYPE_SOCIAL);
        break;
      case CMD_CLAP:
        roomp->playsound(SOUND_CLAP, SOUND_TYPE_SOCIAL);
        break;
      case CMD_FART:
        roomp->playsound(SOUND_FART, SOUND_TYPE_SOCIAL);
        break;
      case CMD_SNEEZE:
        roomp->playsound(SOUND_SNEEZE, SOUND_TYPE_SOCIAL);
        break;
      case CMD_CACKLE:
        roomp->playsound(SOUND_CACKLE, SOUND_TYPE_SOCIAL);
        break;
      case CMD_SCREAM:
        roomp->playsound(SOUND_SCREAM, SOUND_TYPE_SOCIAL);
        break;
      case CMD_CHORTLE:
        roomp->playsound(SOUND_DM_LAUGH, SOUND_TYPE_SOCIAL);
        break;
      case CMD_DISAGREE:
        roomp->playsound(SOUND_DISAGREE, SOUND_TYPE_SOCIAL);
        break;
      case CMD_WOO:
        roomp->playsound(SOUND_YAHOO, SOUND_TYPE_SOCIAL);
        break;
      default:
        break;  
    }
  }

  if (!roomp)
    return FALSE;

  if (buf.empty()) {
    sendTo(action.char_no_arg);
    sendTo("\n\r");
    act(action.others_no_arg, action.hide, this, 0, 0, TO_ROOM);

    for (t = roomp->getStuff(); t; t = t2) {
      t2 = t->nextThing;
      tmp = dynamic_cast<TMonster *>(t);
      if (!tmp)
        continue;

      rc = tmp->aiSocialSwitch(this, NULL, cmd, TARGET_NONE);

      if (IS_SET_DELETE(rc, DELETE_THIS)) {
        delete tmp;
        tmp = NULL;
      }

      if (IS_SET_DELETE(rc, DELETE_VICT))
        return DELETE_THIS;
    }

    return FALSE;
  }

  if (!(vict = get_char_room_vis(this, buf, NULL, EXACT_YES))) {
    if (!(vict = get_char_room_vis(this, buf))) {
      if (!(tvict = get_obj_vis_accessible(this, buf))) {
	sendTo(action.not_found);
	sendTo("\n\r");
	return FALSE;
      }
    }
  }
	  

  if (vict && vict == this) {
    sendTo(action.char_auto);
    sendTo("\n\r");
    act(action.others_auto, action.hide, this, 0, 0, TO_ROOM);

    for (t = roomp->getStuff(); t; t = t2) {
      t2 = t->nextThing;
      tmp = dynamic_cast<TMonster *>(t);

      if (!tmp)
        continue;

      rc = tmp->aiSocialSwitch(this, NULL, cmd, TARGET_SELF);

      if (IS_SET_DELETE(rc, DELETE_THIS)) {
        delete tmp;
        tmp = NULL;
      }

      if (IS_SET_DELETE(rc, DELETE_VICT))
        return DELETE_THIS;
    }
  } else if(vict){
    if (vict->getPosition() < action.minPos)
      act("$N is not in a proper position for that.", FALSE, this, 0, vict, TO_CHAR);
    else {
      if (socialLimbBad(vict,cmd))
        return FALSE;

      act(action.char_found, 0, this, 0, vict, TO_CHAR);
      act(action.others_found, action.hide, this, 0, vict, TO_NOTVICT);
      act(action.vict_found, action.hide, this, 0, vict, TO_VICT);

      for (t = roomp->getStuff(); t; t = t2) {
        t2 = t->nextThing;
        tmp = dynamic_cast<TMonster *>(t);

        if (!tmp)
          continue;

        if (tmp == vict)
          rc = tmp->aiSocialSwitch(this, NULL, cmd, TARGET_MOB);
        else
          rc = tmp->aiSocialSwitch(this, vict, cmd, TARGET_OTHER);

        if (IS_SET_DELETE(rc, DELETE_THIS)) {
          delete tmp;
          tmp = NULL;
        }

        if (IS_SET_DELETE(rc, DELETE_VICT))
          return DELETE_THIS;
      }
    }
  } else if(tvict){
    act(action.char_found, 0, this, 0, tvict, TO_CHAR);
    act(action.others_found, action.hide, this, 0, tvict, TO_NOTVICT);
  }    

  return FALSE;
}

void TBeing::doInsult(const char *argument)
{
  char buf[100];
  char arg[MAX_INPUT_LENGTH];
  TBeing *victim;

  strcpy(arg, argument);

  if (*arg) {
    if (!(victim = get_char_room_vis(this, arg))) 
      sendTo("Can't hear you!\n\r");
    else {
      if (victim != this) {
        sprintf(buf, "You insult %s.\n\r", victim->getName());
        sendTo(buf);

        switch (::number(0, 2)) {
          case 0:
            if (getSex() == SEX_MALE) {
              if (victim->getSex() == SEX_MALE)
                act("$n accuses you of fighting like a woman!", FALSE, this, 0, victim, TO_VICT);
              else
                act("$n says that women can't fight.", FALSE, this, 0, victim, TO_VICT);
            } else {        
              if (victim->getSex() == SEX_MALE)
                act("$n accuses you of having the smallest.... (brain?)",
                   FALSE, this, 0, victim, TO_VICT);
              else
                act("$n tells you that you'd lose a beauty contest against a troll.",
                   FALSE, this, 0, victim, TO_VICT);
            }
            break;
          case 1:
            act("$n calls your mother a bitch!", FALSE, this, 0, victim, TO_VICT);
            break;
          default:
            act("$n tells you to get lost!", FALSE, this, 0, victim, TO_VICT);
            break;
        }                
        act("$n insults $N.", TRUE, this, 0, victim, TO_NOTVICT);
      } else
        sendTo("You feel insulted.\n\r");
    }
  } else
    sendTo("Sure you don't want to insult everybody.\n\r");
}

void TBeing::doScratch(const char *argument)
{
  char arg[256];

  if (in_room < 0)
    return;
  
  strcpy(arg, argument);

  if (!strcasecmp(arg, "leg")) {
    act("$n vigorously scratches $s leg!", TRUE, this, 0, 0, TO_ROOM);
    act("You vigorously scratch your leg!", FALSE, this, 0, 0, TO_CHAR); 
  } else if (!strcasecmp(arg, "ass") || !strcasecmp(arg, "butt")) {
    act("$n vigorously scratches $s butt!", TRUE, this, 0, 0, TO_ROOM);
    act("You vigorously scratch your butt!", FALSE, this, 0, 0, TO_CHAR);
  } else if (!strcasecmp(arg, "crotch")) {
    act("$n vigorously scratches $s genital region!", TRUE, this, 0, 0, TO_ROOM);
    act("You vigorously scratch your genitals!", FALSE, this, 0, 0, TO_CHAR);
  } else if (!strcasecmp(arg, "head")) {
    act("$n vigorously scratches $s head!", TRUE, this, 0, 0, TO_ROOM);
    act("You vigorously scratch your head!", FALSE, this, 0, 0, TO_CHAR);
  } else {
    act("$n vigorously scratches $mself!", TRUE, this, 0, 0, TO_ROOM);
    act("You vigorously scratch yourself!", FALSE, this, 0, 0, TO_CHAR);
  }
}

void TObj::peeMe(const TBeing *, liqTypeT)
{
}

void TPool::peeMe(const TBeing *ch, liqTypeT liq)
{
  act("$n smiles happily as $e pisses into $p.", TRUE, ch, this, NULL, TO_ROOM);
  act("You smile happily as you piss into $p.",  TRUE, ch, this, NULL, TO_CHAR); 

  if (ch->isImmortal() && getDrinkType() == LIQ_WATER) {
    act("$e turns water to wine!", TRUE, ch, this, NULL, TO_ROOM);
    act("You turn water to wine!", TRUE, ch, this, NULL, TO_CHAR);
    setDrinkType(LIQ_RED_WINE);
  } else 
    fillMe(ch, liq);
}

void TPlant::peeOnMe(const TBeing *ch)
{
  act("$n smiles happily as $e pisses all over $p.", TRUE, ch, this, NULL, TO_ROOM);
  act("You smile happily as you piss all over $p.",  TRUE, ch, this, NULL, TO_CHAR); 
  
  if(ch->isImmortal())
    updateAge();
}

void TBeing::doCombine(const sstring &arg)
{
  sendTo("You don't even know where to being with that.\n\r");
}

void TBeing::doPoop(void)
{
  TObj *obj=NULL;

#if 0
  if(isPc() && !isImmortal()){
    sendTo("Hey buddy, this isn't ScatMUD.  Mind your manners.\n\r");
    return;
  }
#endif

  if(getCond(POOP) <= 0 && !isImmortal()){
    sendTo("You don't have to go poop right now.\n\r");
    return;
  }
  
  if(!roomp){
    sendTo("You can't go poop unless you're in a room.\n\r");
    return;
  }

  if(equipment[WEAR_WAISTE]||equipment[WEAR_LEGS_R]||equipment[WEAR_LEGS_L]){
    sendTo("You can't go poop with pants or a belt on!\n\r");
    return;
  }

  if(!(obj=read_object(OBJ_PILE_OFFAL, VIRTUAL))){
    vlogf(LOG_BUG, "problem loading offal in doPoop()");
    return;
  }

  act("$n <o>defecates<z> on the $g.",
      TRUE, this, NULL, NULL, TO_ROOM);
  act("You <o>defecate<z> on the $g.",
      TRUE, this, NULL, NULL, TO_CHAR);

  if(isPc()){
    obj->setWeight(getCond(POOP)/10.0);
    obj->setVolume(getCond(POOP));
  }


  int weightmod=1;

  if(!::number(0,9)){
    if(!::number(0,9)){
      if(!::number(0,9)){
	act("That really did <r>HURT<1>!",
	    TRUE, this, NULL, NULL, TO_CHAR);
	weightmod = 8;
      } else {
	act("You feel like you really nailed that one.",
	    TRUE, this, NULL, NULL, TO_CHAR);
	weightmod = 4;
      }
    } else {
      act("Whoa, that was a big one!",
	  TRUE, this, NULL, NULL, TO_CHAR);
      weightmod = 2;
    }
  }
  
  obj->setWeight(obj->getWeight() * (float)weightmod);
  obj->setVolume(obj->getVolume() * weightmod);
  

  obj->setWeight(obj->getWeight() + ::number(0, (int)obj->getWeight()));
  obj->setVolume(obj->getVolume() + ::number(0, obj->getVolume()));



  *this->roomp += *obj;

  setCond(POOP, 0);

  return;
}

void TBeing::doPee(const sstring &argument)
{
  TThing *t;
  TObj *o;
  sstring arg=argument;
  liqTypeT liquid=LIQ_URINE;
  int amt=::number(1,10);
  sstring whitespace=" \f\n\r\t\v";

  TBeing *mob;

  if (in_room < 0)
    return;
  
  if (!hasWizPower(POWER_PEE)){
    amt=getCond(PEE);
    if(amt <= 0){
      sendTo("You don't have to go pee right now.\n\r");
      return;
    }

    if(getSex() == SEX_FEMALE && (equipment[WEAR_WAISTE] ||
       equipment[WEAR_LEGS_R] || equipment[WEAR_LEGS_L])){
      sendTo("You can't go pee with pants or a belt on!\n\r");
      return;
    }
  } else {
    for(liquid=MIN_DRINK_TYPES;liquid<MAX_DRINK_TYPES;liquid++){
      if(is_abbrev(arg, stripColorCodes(liquidInfo[liquid]->name)))
	break;
    }
    if(liquid==MAX_DRINK_TYPES)
      liquid=LIQ_LEMONADE;
    else
      arg="";
  }


  if(arg.substr(0,3) == "in "){
    arg.erase(0,3);
    
    if(arg=="" || !(o=generic_find_obj(arg,FIND_OBJ_INV|FIND_OBJ_ROOM,this))){
      sendTo("What do you want to pee into?\n\r");
      return;
    } else {
      o->peeMe(this, liquid);
    }
  } else if(!arg.empty()){
    if(!(t = searchLinkedListVis(this, arg,roomp->getStuff()))){
      sendTo("What do you want to pee on?\n\r");
      return;
    } else {
      t->peeOnMe(this);
    }
  } else {
    act("$n quietly relieves $mself.  You are not amused.",
	TRUE, this, NULL, NULL, TO_ROOM);
    sendTo("You relieve yourself as stealthfully as possible.\n\r");
    dropPool(amt, liquid);
  }

  setCond(PEE, 0);
}

void TBeing::doTip(const sstring &arg)
{
  TThing *t;
  sstring hat=equipment[WEAR_HEAD]?fname(equipment[WEAR_HEAD]->name):"hat";

  if(arg.empty()){
    act(fmt("You tip your %s.") % hat,  FALSE, this, NULL, NULL, TO_CHAR);
    act(fmt("$n tips $s %s.") % hat,  FALSE, this, NULL, NULL, TO_ROOM);
  } else {
    t=searchLinkedList(arg, roomp->getStuff(), TYPETHING);
    if (!t)
      sendTo(fmt("Tip your %s to whom?\n\r") % hat);
    else if(t==this){
      act(fmt("You tip your %s to yourself - are you feeling alright?") % hat,
	  FALSE, this, NULL, t, TO_CHAR);
      act(fmt("$n tips $s %s to $mself? Don't ask...") % hat,
	  FALSE, this, NULL, t, TO_ROOM);
    } else {
      act(fmt("You tip your %s in acknowledgement of $N.") % hat, 
	  FALSE, this, NULL, t, TO_CHAR);
      act(fmt("$n tips $s %s to $N.") % hat,
	  FALSE, this, NULL, t, TO_NOTVICT);
      act(fmt("$n tips $s %s to you.") % hat,
	  FALSE, this, NULL, t, TO_VICT);
    }
  }
}

void TBeing::doPoke(const sstring &arg)
{
  TThing *t = NULL;
  TThing *hold = NULL;
  TObj *obj;
  TBeing *b;
  sstring holdBuf, buf;

  if (!roomp)
    return;

  if ((hold = equipment[getPrimaryHold()])) 
    buf=fname(hold->name);
  else
    buf="finger";

  if (arg.empty()) {
    holdBuf = fmt("You point your %s around threateningly.") % buf;
    act(holdBuf, FALSE, this, NULL, this, TO_CHAR);
    holdBuf = fmt("$n points $s %s around threateningly.") % buf;
    act(holdBuf, FALSE, this, NULL, this, TO_ROOM);
    return;
  }
  // point at someone
  for (t = roomp->getStuff(); t; t = t->nextThing) {
    if (isname(arg, t->name)) {
      obj = dynamic_cast<TObj *>(t);
      if (obj) {
	sendTo(COLOR_OBJECTS, fmt("You carefully prod %s with your %s.\n\r") % obj->getName() % buf);
	holdBuf = fmt("$n carefully prods $N with $s %s.") % buf;
	act(holdBuf, FALSE, this, NULL, obj, TO_ROOM);
        return;
      } 
      b = dynamic_cast<TBeing *>(t);
      if (b) {
        if (b == this) {
          sendTo("You poke yourself in the ribs, feeling very silly.\n\r");
          act("$n pokes $mself in the ribs, looking very sheepish.", FALSE, this, NULL, NULL, TO_ROOM);
        } else {
	  sendTo(COLOR_OBJECTS,fmt("You poke %s in the ribs with your %s.\n\r") % b->getName() % buf);
	  holdBuf = fmt("$n pokes %s in the ribs with $s %s.") % b->getName() %buf;
	  act(holdBuf, FALSE, this, NULL, b, TO_NOTVICT);
	  holdBuf = fmt("$n pokes you in the ribs with $s %s.") % buf;
	  act(holdBuf, FALSE, this, NULL, b,TO_VICT);
	}
        return;
      }
    }
  }
  // If we got here, the person pointed at something that wasnt in the room
  sendTo("You look for something to poke, but come up disappointed.\n\r");
}

void TBeing::doPoint(const sstring &arg)
{
  TThing *t = NULL;
  TThing *hold = NULL;
  TObj *obj;
  TBeing *b;
  sstring holdBuf, buf;

  if (!roomp)
    return;


  if ((hold = equipment[getPrimaryHold()])) 
    buf=fname(hold->name);
  else
    buf="finger";

  if (arg.empty()) {
    holdBuf = fmt("You point your %s around randomly.") % buf;
    act(holdBuf, FALSE, this, NULL, this, TO_CHAR);
    holdBuf = fmt("$n points $s %s around randomly.") % buf;
    act(holdBuf, FALSE, this, NULL, this, TO_ROOM);
    return;
  }

  // point in a direction
  dirTypeT dir = getDirFromChar(arg);
  if (dir != DIR_NONE) {
    sendTo(fmt("You point your %s %s.\n\r") % buf % dirs_to_blank[dir]);
    act(fmt("$n points $s %s %s.") % buf % dirs_to_blank[dir],
	false, this, NULL, NULL, TO_ROOM);
    return;
  }

  // point at someone
  for (t = roomp->getStuff(); t; t = t->nextThing) {
    if (isname(arg, t->name)) {
      obj = dynamic_cast<TObj *>(t);
      if (obj) {
        sendTo(COLOR_OBJECTS,fmt("You point your %s at %s.\n\r") % 
	       buf % obj->getName());
        holdBuf = fmt("$n points $s %s at %s.") % buf % obj->getName();
        act(holdBuf, FALSE, this, obj, NULL, TO_ROOM);
        return;
      } 
      b = dynamic_cast<TBeing *>(t);
      if (b) {
        if (b == this) {
          sendTo("You point at yourself.\n\r");
          act("$n points at $mself.", FALSE, this, NULL, NULL, TO_ROOM);
        } else {
	  sendTo(COLOR_OBJECTS, fmt("You point at %s with your %s.\n\r") % 
		 b->getName() % buf);
	  holdBuf = fmt("$n points at %s with $s %s.") % b->getName() % buf;
	  act(holdBuf, FALSE, this, NULL, b, TO_NOTVICT);
	  holdBuf = fmt("$n points at you with $s %s.") % buf;
	  act(holdBuf, FALSE, this, NULL, b, TO_VICT);
	}
        return;
      }
    }
  }
  // If we got here, the person pointed at something that wasnt in the room
  sendTo("Do you usually point at things that aren't there?\n\r");
}



int TBeing::doBite(const sstring &arg)
{
  TThing *t = NULL;
  TBeing *b;
  sstring buf;
  int rc;

  if (!roomp)
    return FALSE;

  if (arg.empty()) {
    sendTo("Whom do you want to bite?\n\r");
    return FALSE;
  }


  if(isVampire()){
    // vampire bite!
    if (!(b = get_char_room_vis(this, arg))) {
      if (!(b = fight())) {
	sendTo("Whose blood do you wish to suck?\n\r");
	return FALSE;
      }
    }
    if (!sameRoom(*b)) {
      sendTo("That person isn't around.\n\r");
      return FALSE;
    }
    if (b == this) {
      sendTo("Sucking blood from yourself would not be effective.\n\r");
      return FALSE;
    }
    
    if (checkPeaceful("You feel too peaceful to contemplate violence.\n\r"))
      return FALSE;
    
    if (noHarmCheck(b))
      return FALSE;

    if(b->isImmortal()){
      sendTo("That would be unwise.\n\r");
      return FALSE;
    }

    if (b->isUndead() || b->isColdBlooded()) {
      sendTo("You can only do this to living, warm blooded opponents.\n\r");
      return FALSE;
    }
    
    if(b->getPosition() <= POSITION_INCAP){
      sendTo("That victim is too close to death already.\n\r");
      return FALSE;
    }


    reconcileDamage(b, 0, DAMAGE_DRAIN);
    
    if((((b->hitLimit() < hitLimit()) && 
	(GetMaxLevel()>b->GetMaxLevel()+10)) &&
	hits(b, attackRound(b) - b->defendRound(this))) ||
       isImmortal()){
      act("You sink your fangs deep into $N's neck and suck $S <r>blood<1>!",
	  FALSE, this, NULL, b, TO_CHAR);
      act("$n sinks $s fangs deep into $N's neck and sucks $S <r>blood<1>!",
	  FALSE, this, NULL, b, TO_NOTVICT);
      act("$n sinks $s fangs deep into your neck and sucks your <r>blood<1>!",
	  FALSE, this, NULL, b, TO_VICT);

      rc = reconcileDamage(b, b->getHit()+5, DAMAGE_DRAIN);
      b->setHit(-5); // sometimes the above doesn't set to -5 properly

      gainCondition(FULL, 15);
      gainCondition(THIRST, 15);
      act("You feel satiated.", FALSE, this, NULL, b, TO_CHAR);

      act("You reel about unsteadily, flush with <r>blood<1>.",
	  FALSE, this, NULL, b, TO_CHAR);

      if(fight()) {
	stopFighting();
	b->stopFighting();
      }

      if(b->isPc() && !b->hasQuestBit(TOG_VAMPIRE) &&
	 !b->hasQuestBit(TOG_BITTEN_BY_VAMPIRE) && !b->isVampire()){
	affectedData aff;
	aff.type = AFFECT_BITTEN_BY_VAMPIRE;
	aff.location = APPLY_NONE;
	aff.duration = 24 * UPDATES_PER_MUDHOUR;
	
	b->affectTo(&aff);
      }


      addToWait(combatRound(5));

      return rc;
    } else {
      act("You try to bite $N's neck but $E fights you off!",
	  FALSE, this, NULL, b, TO_CHAR);
      act("$n tries to bite $N's neck, but $N fights $m off!",
	  FALSE, this, NULL, b, TO_NOTVICT);
      act("$n tries to bite your neck, but you fight him off!",
	  FALSE, this, NULL, b, TO_VICT);
      return TRUE;
    }
  } else if(getMyRace()->isLycanthrope()){
    // we don't use isLycanthrope() here because we don't want non-transformed
    // players to be able to bite
    // were-creature bite!
    if (!(b = get_char_room_vis(this, arg))) {
      if (!(b = fight())) {
	sendTo("Who do you want to bite?\n\r");
	return FALSE;
      }
    }
    if (!sameRoom(*b)) {
      sendTo("That person isn't around.\n\r");
      return FALSE;
    }
    if (b == this) {
      sendTo("Biting yourself would not be wise.\n\r");
      return FALSE;
    }
    
    if (checkPeaceful("You feel too peaceful to contemplate violence.\n\r"))
      return FALSE;
    
    if (noHarmCheck(b))
      return FALSE;

    if(b->isImmortal()){
      sendTo("That would be unwise.\n\r");
      return FALSE;
    }

    reconcileDamage(b, 0, DAMAGE_STOMACH_WOUND);
    
    if(hits(b, attackRound(b) - b->defendRound(this))){
      act("You sink your teeth into $N's flesh and tear it viciously!",
	  FALSE, this, NULL, b, TO_CHAR);
      act("$n sinks $s teeth into $N's flesh and tears at it viciously!",
	  FALSE, this, NULL, b, TO_NOTVICT);
      act("$n sinks $s teeth into your flesh and tears at it viciously!",
	  FALSE, this, NULL, b, TO_VICT);

      rc = reconcileDamage(b, ::number(5,25), DAMAGE_STOMACH_WOUND);

      if(b->isPc() && !b->isLycanthrope()){	
	act("You feel a burning in your <r>blood<1>.",
	    FALSE, this, NULL, b, TO_VICT);
	b->setQuestBit(TOG_LYCANTHROPE);
      }

      addToWait(combatRound(3));

      return rc;
    } else {
      act("You try to bite $N but $E fights you off!",
	  FALSE, this, NULL, b, TO_CHAR);
      act("$n tries to bite $N, but $N fights $m off!",
	  FALSE, this, NULL, b, TO_NOTVICT);
      act("$n tries to bite you, but you fight him off!",
	  FALSE, this, NULL, b, TO_VICT);
      return TRUE;
    }
  } else {
    for (t = roomp->getStuff(); t; t = t->nextThing) {
      if (isname(arg, t->name)) {
	if((b=dynamic_cast<TBeing *>(t)) && b==this){
	  sendTo(COLOR_OBJECTS, "You bite yourself. Are you that deranged?\n\r");
	  act("$n bites himself. WEIRD?!?", FALSE, this, NULL, b, TO_NOTVICT);
	} else if(b){
	  sendTo(COLOR_OBJECTS, fmt("You rip %s's flesh with your piercing bite.\n\r") %
		 b->getName());
	  act("$n sinks $s teeth into $N. $N screams in agony!",
	      FALSE, this, NULL, b, TO_NOTVICT);
	  act("$n bites you. OOOOOOOOOHHHHHHHHHHHH that hurts!",
	      FALSE, this, NULL, b, TO_VICT);
	}
	return TRUE;
      }
    }
  }

  sendTo("How about biting someone?.\n\r");

  return FALSE;
}
