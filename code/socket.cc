
//////////////////////////////////////////////////////////////////////////
//
//   SneezyMUD - All rights reserved, SneezyMUD Coding Team
//
//   "socket.cc" - All methods for TSocket class
//               
//
//////////////////////////////////////////////////////////////////////////

#include <csignal>
#include <cstdarg>

extern "C" {
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/param.h>

#ifdef SOLARIS
#include <sys/file.h>
#endif

int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);   
}

#include "stdsneezy.h"
#include "statistics.h"
#include "database.h"
#include "spelltask.h"
#include "systemtask.h"
#include "socket.h"
#include "weather.h"
#include "obj_smoke.h"
#include "obj_vehicle.h"
#include "pathfinder.h"

int maxdesc, avail_descs;  
bool Shutdown = 0;               // clean shutdown
int tics = 0;
TSocket *gSocket;
long timeTill = 0;
Descriptor *descriptor_list = NULL, *next_to_process; 

struct timeval timediff(struct timeval *a, struct timeval *b)
{
  struct timeval tmp, rslt;

  tmp = *a;

  if ((rslt.tv_usec = tmp.tv_usec - b->tv_usec) < 0) {
    rslt.tv_usec += 1000000;
    --(tmp.tv_sec);
  }
  if ((rslt.tv_sec = tmp.tv_sec - b->tv_sec) < 0) {
    rslt.tv_usec = 0;
    rslt.tv_sec = 0;
  }
  return rslt;
}

void TSocket::addNewDescriptorsDuringBoot(sstring tStString)
{
  fd_set input_set, output_set, exc_set;
  static struct timeval null_time;
  Descriptor *point;
  static bool been_called = false;

#ifndef SOLARIS
  static int mask;
#endif

  if (!been_called) {
    // prepare the time values 
    null_time.tv_sec = 0;
    null_time.tv_usec = 0;

    maxdesc = m_sock;
    avail_descs = 150;

#ifndef SOLARIS
    mask = sigmask(SIGUSR1) | sigmask(SIGUSR2) | sigmask(SIGINT) |
// blah, trapping PROF PREVENTS the timing signals from working!
//        sigmask(SIGPROF) |  // needed for profile code
// sigmask(SIGALRM) |
        sigmask(SIGPIPE) | sigmask(SIGTERM) |
        sigmask(SIGURG) | sigmask(SIGXCPU) | sigmask(SIGHUP);
#endif

    been_called = true;
  }

  // Check what's happening out there 
  FD_ZERO(&input_set);
  FD_ZERO(&output_set);
  FD_ZERO(&exc_set);
  FD_SET(m_sock, &input_set);
  for (point = descriptor_list; point; point = point->next) {
    FD_SET(point->socket->m_sock, &input_set);
    FD_SET(point->socket->m_sock, &exc_set);
    FD_SET(point->socket->m_sock, &output_set);
  }

#ifndef SOLARIS
  sigsetmask(mask);
#endif
#ifdef LINUX
  // linux uses a nonstandard style of "timedout" (the last parm of select)
  // it gets hosed each select() so must be reinited here
  null_time.tv_sec = 0;
  null_time.tv_usec = 0;
#endif
  if (select(maxdesc + 1, &input_set, &output_set, &exc_set, &null_time) < 0) {
    perror("Error in Select (poll)");
    return;
  }


#ifndef SOLARIS
  sigsetmask(0);
#endif

  // establish any new connections 
  if (FD_ISSET(m_sock, &input_set)) {
    int tFd;

    if ((tFd = newDescriptor()) < 0)
      perror("New connection");
    else if (!tStString.empty() && tFd)
      descriptor_list->writeToQ(tStString);
  }
  // close any connections with an exceptional condition pending 
  for (point = descriptor_list; point; point = next_to_process) {
    next_to_process = point->next;
    if (FD_ISSET(point->socket->m_sock, &exc_set)) {
      FD_CLR(point->socket->m_sock, &input_set);
      FD_CLR(point->socket->m_sock, &output_set);
      delete point;
    }
  }
  // read any incoming input, and queue it up 
  for (point = descriptor_list; point; point = next_to_process) {
    next_to_process = point->next;
    if (FD_ISSET(point->socket->m_sock, &input_set)) {
      if (point->inputProcessing() < 0) {
        delete point;
        point = NULL;
      }
    }
  }
}



// updates the data in the wholist table in the database
// returns the count of players logged in now
int updateWholist()
{
  Descriptor *p;
  TPerson *p2;
  int count = 0;
  static int last_count;
  static sstring wholist_last;
  sstring wholist = "";

  for (p = descriptor_list; p; p = p->next) {
    if (p && p->connected == CON_PLYNG || p->connected > MAX_CON_STATUS && p->character && 
	p->character->name && p->character->isPc() && !p->character->isLinkdead() && p->character->polyed == POLY_TYPE_NONE) {
      if ((p2 = dynamic_cast<TPerson *>(p->character))) {
	wholist += p2->getName();
      }
    }
  }


  if (wholist != wholist_last) {
    // every 10 RL seconds
    TDatabase db(DB_SNEEZY);
    
    
    db.query("delete from wholist where port=%i", gamePort);
    
    //  vlogf(LOG_DASH, fmt("Updating who table for port %d") %  gamePort);
  for (p = descriptor_list; p; p = p->next) {
    if (p && p->connected == CON_PLYNG || p->connected > MAX_CON_STATUS && p->character &&
        p->character->name && p->character->isPc() && !p->character->isLinkdead() && p->character->polyed == POLY_TYPE_NONE) {
      if ((p2 = dynamic_cast<TPerson *>(p->character))) {
	  db.query("insert into wholist (name, title, port, invis) VALUES('%s', '%s', %i, %i)", p2->getName(), p2->title,  gamePort, (p2->getInvisLevel() >MAX_MORT)?1:0);
	  count++;
	}
      }
    }
  } else {
    return last_count;
  }
  wholist_last = wholist;
  last_count = count;
  return count;
}

// updates the usagelogs table in the database
// takes the count of players currently logged on
void updateUsagelogs(int count)
{
  time_t ct=time(0);
  static time_t logtime;
  static time_t lastlog;

  int TIME_BETWEEN_LOGS = 300;
  
  // every 10 RL seconds
  TDatabase db(DB_SNEEZY);




  if(logtime/TIME_BETWEEN_LOGS < ct/TIME_BETWEEN_LOGS) {
    //	vlogf(LOG_DASH, fmt("Webstuff: collecting game usage data - %d seconds since last log") %  ct-lastlog);
    //        vlogf(LOG_DASH, fmt("Webstuff:  logtime = %d,  ct = %d, players = %d") %  logtime % ct % count);
    
    
    if (logtime != 0) logtime += TIME_BETWEEN_LOGS;
    else logtime = ct;
    lastlog = ct;
    db.query("insert into usagelogs (time, players, port) VALUES(%i, %i, %i)", logtime, count, gamePort);
    // delete logs older than two months
    db.query("delete from usagelogs where port=%i and time>%i", logtime + 5184000 , gamePort);
  }
}

// check eq of all players online for repo
void checkForRepo(){
  TBeing *tmp_ch, *temp;

  for (tmp_ch = character_list; tmp_ch; tmp_ch = temp) {
    temp = tmp_ch->next; 
    int i;
    TThing *repot, *repot2;
    TObj *repoo;
    // check worn equipment
    for (i = MIN_WEAR;i < MAX_WEAR;i++) {
      if (!(repot = tmp_ch->equipment[i]) || !(repoo = dynamic_cast<TObj *>(repot)))
	continue;
      
      repoCheckForRent(tmp_ch, repoo, false);
    }
    // check inventory
    for (repot = tmp_ch->getStuff(); repot; repot = repot2) {
      repot2 = repot->nextThing;
      repoo = dynamic_cast<TObj *>(repot);
      if (!repoo)
	continue;
      
      repoCheckForRent(tmp_ch, repoo, false);
    }
    
  }
}

void nukeInactiveMobs()
{
  unsigned int i;
  for (i = 0; i < zone_table.size(); i++) {
    if (!zone_table[i].isEmpty())
      continue;
    if (zone_table[i].zone_value == 1)
      zone_table[i].nukeMobs();
    if (zone_table[i].zone_value > 0) {
      zone_table[i].zone_value -= 1;
    }
  }
}

// update the average players displayed in "who"
void updateAvgPlayers()
{
  // statistics stuff
  if (time(0) - stats.useage_timer > (1 * SECS_PER_REAL_MIN)) {
    // figure out average user load
    stats.useage_timer = time(0);
    stats.useage_iters++;
    Descriptor *d;
    for (d = descriptor_list; d; d = d->next) {
      if (d->connected == CON_PLYNG || d->connected > MAX_CON_STATUS)
	stats.num_users++;
    }
  }
}

////////////////////////////////////////////
// handle shutdown
////////////////////////////////////////////
bool TSocket::handleShutdown()
{
  sstring buf;
  static bool sent = false;

  if(Shutdown)
    return true;

  if (timeTill  && (timeTill <= time(0))) {
    if (descriptor_list) {
      buf=fmt("%s time has arrived!\n\r") % shutdown_or_reboot();
      descriptor_list->worldSend(buf, NULL);
      descriptor_list->outputProcessing();
    }
    return true;
  } else if (timeTill && !((timeTill - time(0)) % 60)) {
    int minutes=(timeTill - time(0)) / 60;
    if (!sent) {
      buf="<r>******* SYSTEM MESSAGE ******<z>\n\r";
      buf+=fmt("<c>%s in %ld minute%s.<z>\n\r") % 
	shutdown_or_reboot() % minutes % ((minutes == 1) ? "" : "s");
      descriptor_list->worldSend(buf, NULL);
    }
    sent = true;
  } else if (timeTill && ((timeTill- time(0)) <= 5)) {
    long secs = timeTill - time(0);
    if (!sent) {
      buf="<r>******* SYSTEM MESSAGE ******<z>\n\r";
      buf+=fmt("<c>%s in %ld second%s.<z>\n\r") %
	shutdown_or_reboot() % secs % ((secs == 1) ? "" : "s");
      descriptor_list->worldSend(buf, NULL);
      sent = true;
    }
  } else
    sent = false;

  return false;
}


int TSocket::gameLoop()
{
  fd_set input_set, output_set, exc_set;
  struct timeval last_time, now, timespent, timeout, null_time;
  static struct timeval opt_time;
  Descriptor *point;
  int pulse = 0;
  int teleport=0, combat=0, drowning=0, special_procs=0, update_stuff=0;
  int pulse_tick=0, pulse_mudhour=0, mobstuff=0, quickpulse=0, wayslowpulse=0;
  TBeing *tmp_ch, *temp;
  TObj *obj, *next_thing;
  int rc = 0;
  time_t lagtime_t = time(0);
  TVehicle *vehicle;
  int vehiclepulse = 0;
  sstring str;
  int count;
  int mask;

  // prepare the time values 
  null_time.tv_sec = 0;
  null_time.tv_usec = 0;
  opt_time.tv_usec = OPT_USEC;
  opt_time.tv_sec = 0;
  gettimeofday(&last_time, NULL);

  avail_descs = 150;		
  
  mask = sigmask(SIGUSR1) | sigmask(SIGUSR2) | sigmask(SIGINT) |
    sigmask(SIGPIPE) | sigmask(SIGTERM) |
    sigmask(SIGURG) | sigmask(SIGXCPU) | sigmask(SIGHUP);

  // players may have connected before this point via 
  // addNewDescriptorsDuringBoot, so send all those descriptors the login
  for (point = descriptor_list; point; point = point->next)
    if (!point->m_bIsClient)
      point->sendLogin("1");

  time_t ticktime = time(0);

  while (!handleShutdown()) {
    ////////////////////////////////////////////
    // do some socket stuff or something
    ////////////////////////////////////////////
    // Check what's happening out there 
    FD_ZERO(&input_set);
    FD_ZERO(&output_set);
    FD_ZERO(&exc_set);
    FD_SET(m_sock, &input_set);
    for (point = descriptor_list; point; point = point->next) {
      FD_SET(point->socket->m_sock, &input_set);
      FD_SET(point->socket->m_sock, &exc_set);
      FD_SET(point->socket->m_sock, &output_set);
    }
    ////////////////////////////////////////////
    ////////////////////////////////////////////

    ////////////////////////////////////////////
    // do some time related stuff
    ////////////////////////////////////////////
    // check out the time 
    gettimeofday(&now, NULL);
    timespent=timediff(&now, &last_time);
    timeout=timediff(&opt_time, &timespent);


    if(TestCode1){
      str = "";
      if(!combat)
	str += "combat        ";
      if(!update_stuff)
	str += "update_stuff  ";
      if(!pulse_tick)
	str += "pulse_tick    ";
      if(!teleport)
	str += "teleport      ";
      if(!special_procs)
	str += "special_procs ";
      if(!pulse_mudhour)
	str += "pulse_mudhour ";
      if(!mobstuff)
	str += "mobstuff      ";
      if(!drowning)
	str += "drowning      ";
      if(!wayslowpulse)
	str += "wayslowpulse  ";

      vlogf(LOG_MISC, fmt("%i %i) %s = %i") % 
	    pulse % (pulse%12) % str %
	    ((timespent.tv_sec*1000000)+timespent.tv_usec));
    }




    last_time.tv_sec = now.tv_sec + timeout.tv_sec;
    last_time.tv_usec = now.tv_usec + timeout.tv_usec;
    if (last_time.tv_usec >= 1000000) {
      last_time.tv_usec -= 1000000;
      last_time.tv_sec++;
    }

#ifndef SOLARIS
    sigsetmask(mask);
#endif
#ifdef LINUX
    // linux uses a nonstandard style of "timedout" (the last parm of select)
    // it gets hosed each select() so must be reinited here
    null_time.tv_sec = 0;
    null_time.tv_usec = 0;
#endif

    // this gets our list of socket connections that are ready for handling
    if(select(maxdesc + 1, &input_set, &output_set, &exc_set, &null_time) < 0){
      perror("Error in Select (poll)");
      return (-1);
    }

    // this regulates the speed of the mud
    if (select(0, 0, 0, 0, &timeout) < 0) {
      perror("Error in select (sleep)");
    }

#ifndef SOLARIS
    sigsetmask(0);
#endif
    ////////////////////////////////////////////
    ////////////////////////////////////////////

    ////////////////////////////////////////////
    // establish any new connections 
    ////////////////////////////////////////////
    if (FD_ISSET(m_sock, &input_set)) {
      int rc = newDescriptor();
      if (rc < 0)
	perror("New connection");
      else if (rc) {
        // we created a new descriptor
        // so send the login to the first desc in list
        if (!descriptor_list->m_bIsClient)
          descriptor_list->sendLogin("1");
      }
    }
    ////////////////////////////////////////////
    ////////////////////////////////////////////

    ////////////////////////////////////////////
    // close any connections with an exceptional condition pending 
    ////////////////////////////////////////////
    for (point = descriptor_list; point; point = next_to_process) {
      next_to_process = point->next;
      if (FD_ISSET(point->socket->m_sock, &exc_set)) {
	FD_CLR(point->socket->m_sock, &input_set);
	FD_CLR(point->socket->m_sock, &output_set);
	delete point;
      }
    }
    ////////////////////////////////////////////
    ////////////////////////////////////////////

    ////////////////////////////////////////////
    // read any incoming input, and queue it up 
    ////////////////////////////////////////////
    for (point = descriptor_list; point; point = next_to_process) {
      next_to_process = point->next;
      if (FD_ISSET(point->socket->m_sock, &input_set)) {
	if (point->inputProcessing() < 0) {
	  delete point;
          point = NULL;
        }
      }
    }
    processAllInput();
    setPrompts(output_set);
    afterPromptProcessing(output_set);
    ////////////////////////////////////////////
    ////////////////////////////////////////////

    ////////////////////////////////////////////
    // setup the pulse boolean flags
    ////////////////////////////////////////////
    // we should spread out these pulses among out cycles
    // since we work on multiples of 12, that gives us 12 pulse lines
    // in order to do some load balanced along our cycles,
    // I've sorted these into 3 groups of 3 -
    // one group of fast pulses (12), one group of medium pulses (36,48)
    // and one group of slow pulses (360, 1440, 17280)
    // then reordered those groups in evenly mixed groups
    // of 1 fast, 1 medium and 1 slow, then spread those out evenly
    // along a 12 pulse cycle, like so:
    // 0 - combat;
    // 1 - drowning;
    // 2 - pulsetick;
    // 3 - <none>;
    // 4 - teleport;
    // 5 - specproc;
    // 6 - pulsemudhour;
    // 7 - <none>
    // 8 - mobstuff;
    // 9 - updatestuff;
    // 10 - wayslow;
    // 11 - <none>
    // new pulses can be inserted into the <none> slots if needed
    // additionally, medium and slow pulses can be offset by additional
    // multiples of 12 to seperate themselves from the same 12 pulse cycle
    // this takes a little bit of hand tuning I guess
    
    pulse++;

    // undocumented beavior requires all pulses to be multiples of 12
#if 0
    // quickpulse is ignored because it is too fast!
    quickpulse = (pulse % ONE_SECOND/5);           // 2

    combat = ((pulse+0) % PULSE_COMBAT);               // 12
    update_stuff = ((pulse+1) % PULSE_NOISES);         // 48
    pulse_tick = ((pulse+2) % PULSE_UPDATE);           // 360
    // 3 - empty
    teleport = ((pulse+4) % PULSE_TELEPORT);           // 12
    pulse_mudhour = ((pulse+5) % PULSE_MUDHOUR);       // 1440
    special_procs = ((pulse+6+12) % PULSE_SPEC_PROCS); // 36
    // 7 - empty
    mobstuff = ((pulse+8) % PULSE_MOBACT);             // 12
    drowning = ((pulse+9) % PULSE_DROWNING);           // 36
    wayslowpulse = ((pulse+10) % (PULSE_MUDHOUR * 12)); // 17280
    // 11 - empty
#endif

    teleport = (pulse % PULSE_TELEPORT);
    combat = (pulse % PULSE_COMBAT);
    drowning = (pulse % PULSE_DROWNING);
    special_procs = (pulse % PULSE_SPEC_PROCS);
    update_stuff = (pulse % PULSE_NOISES);
    pulse_mudhour = (pulse % PULSE_MUDHOUR);
    mobstuff = (pulse % PULSE_MOBACT);
    pulse_tick = (pulse % PULSE_UPDATE);
    quickpulse = (pulse % ONE_SECOND/5);
    wayslowpulse = (pulse % (PULSE_MUDHOUR * 12));

    ////////////////////////////////////////////
    ////////////////////////////////////////////

    
    // interport communication
    mudRecvMessage();

    // send out repo mobs
    if(!wayslowpulse){
      checkForRepo();
    }

    if (!pulse_tick) {
      // these are done per tick (15 mud minutes)
      doGlobalRoomStuff();
      deityCheck(FALSE);
      apocCheck();
      save_factions();
      save_newfactions();
      weatherAndTime(1);

      count=updateWholist();
      updateUsagelogs(count);
    }

    if (!combat){
      perform_violence(pulse);
    }

    if (!pulse_mudhour) {
      // these are done per mud hour
      recalcFactionPower();

      // adjust zones for nuking
      if (nuke_inactive_mobs)
	nukeInactiveMobs();

      // weather and time stuff
      if (time_info.hours == 1)
	update_time();

      zone_update();
      do_components(-1);
      launch_caravans();
      updateAvgPlayers();
      checkGoldStats();
    }

    
    if(!teleport){
      for (unsigned int i = 0; i < zone_table.size(); i++)
	zone_table[i].zone_value=zone_table[i].isEmpty()?1:-1;

      call_room_specials();
    }


    if (!teleport){
      // note on this loop
      // it is possible that next_thing gets deleted in one of the sub funcs
      // we don't get acknowledgement of this in any way.
      // to avoid problems this might cause, we reinitialize at
      // the end (eg, before any deletes, or before we come back around)
      // bottom line is that next_thing keeps getting set because it might be
      // bogus after the function call.

      ++vehiclepulse;

      for (obj = object_list; obj; obj = next_thing) {
	next_thing = obj->next;

        if (!dynamic_cast<TObj *>(obj)) {
          vlogf(LOG_BUG, fmt("Object_list produced a non-obj().  rm: %d") %  obj->in_room);
          vlogf(LOG_BUG, fmt("roomp %s, parent %s") %  
                (obj->roomp ? "true" : "false") %
                (obj->parent ? "true" : "false"));
          // bogus objects tend to have garbage in obj->next
          // it would be dangerous to continue with this loop
          // this is called often enough that one skipped iteration should
          // not be noticed.  Therefore, break out.
          break;
        }

	// vehicle movement
	if((vehicle=dynamic_cast<TVehicle *>(obj)))
	  vehicle->vehiclePulse(vehiclepulse);
	

        if (!combat) {
          rc = obj->detonateGrenade();
          if (IS_SET_DELETE(rc, DELETE_THIS)) {
            next_thing = obj->next;
            delete obj;
            obj = NULL;
            continue;
          }
        }
	if (!teleport) {
          rc = obj->checkFalling();
          if (IS_SET_DELETE(rc, DELETE_THIS)) {
            next_thing = obj->next;
            delete obj;
            obj = NULL;
            continue;
          }
          rc = obj->riverFlow(pulse);
          if (IS_SET_DELETE(rc, DELETE_THIS)) {
            next_thing = obj->next;
            delete obj;
            obj = NULL;
            continue;
          }
	}
	if (!special_procs) {
	  check_sinking_obj(obj, obj->in_room);
          if (obj->spec) {
            rc = obj->checkSpec(NULL, CMD_GENERIC_PULSE, "", NULL);
            if (IS_SET_DELETE(rc, DELETE_ITEM)) {
              next_thing = obj->next;
              delete obj;
              obj = NULL;
              continue;
            }
            if (rc) {
              next_thing = obj->next;
              continue;
            }
          }
	}
	if (!quickpulse) {
          if (obj->spec) {
            rc = obj->checkSpec(NULL, CMD_GENERIC_QUICK_PULSE, "", NULL);
            if (IS_SET_DELETE(rc, DELETE_ITEM)) {
              next_thing = obj->next;
              delete obj;
              obj = NULL;
              continue;
            }
            if (rc) {
              next_thing = obj->next;
              continue;
            }
          }

	  rc = obj->updateBurning();
          if (IS_SET_DELETE(rc, DELETE_THIS)) {
            next_thing = obj->next;
            delete obj;
            obj = NULL;
	    continue;
          }
	  TSmoke *smoke=dynamic_cast<TSmoke *>(obj);
	  
	  // handle merging
	  if(smoke){	    
	    TSmoke *tsmoke;
	    TThing *t, *t2;

	    for(t=real_roomp(smoke->in_room)->getStuff();t;t=t2){
	      t2=t->nextThing;

	      if((tsmoke=dynamic_cast<TSmoke *>(t)) && tsmoke!=smoke){
		// merge!
		smoke->addToVolume(tsmoke->getVolume());
		--(*tsmoke);
		delete tsmoke;
	      }
	    }

	  }


	  // handle drifting
	  if(smoke && smoke->roomp){
	    roomDirData *exitp;
	    TRoom *rp=smoke->roomp;
	    TThing *t, *t2;
	    TPortal *tp;
	    
	    // move up if possible
	    if((exitp=smoke->roomp->exitDir(DIR_UP)) &&
	       !IS_SET(exitp->condition, EX_CLOSED) &&
	       (rp=real_roomp(exitp->to_room))){
	      act("$n drifts upwards.",
		  FALSE, smoke, 0, 0, TO_ROOM); 
	      --(*smoke);
	      *rp += *smoke;
	      act("$n drifts in from below.",
		  FALSE, smoke, 0, 0, TO_ROOM); 
	    } else {
	      dirTypeT dir;
	      TPathFinder path;

	      path.setUsePortals(true);
	      path.setNoMob(false);
	      path.setThruDoors(false);
	      path.setRange(25);
	      dir=path.findPath(smoke->inRoom(), findOutdoors());
	      
	      if(dir >= MAX_DIR){
		dir=dirTypeT(dir-MAX_DIR+1);
		int seen = 0;

		for (t = smoke->roomp->getStuff(); t; t = t2) {
		  t2 = t->nextThing;

		  if ((tp=dynamic_cast<TPortal *>(t))){
		    seen++;
		    if (dir == seen) {
		      if((rp=real_roomp(tp->getTarget()))){
			act(fmt("$n drifts into %s.") % tp->getName(),
			    FALSE, smoke, 0, 0, TO_ROOM); 
			--(*smoke);
			*rp += *smoke;
			act(fmt("$n drifts in from %s.") % tp->getName(),
			    FALSE, smoke, 0, 0, TO_ROOM); 
		      }
		    }
		  }
		}
	      } else if (dir >= MIN_DIR && dir != DIR_DOWN && 
		  (exitp=smoke->roomp->exitDir(dir)) &&
		  (rp=real_roomp(exitp->to_room))){
		act(fmt("$n drifts %s.") % dirs_to_blank[dir],
		    FALSE, smoke, 0, 0, TO_ROOM); 

		--(*smoke);
		*rp += *smoke;		
		act(fmt("$n drifts in from the %s.") % dirs[rev_dir[dir]],
		    FALSE, smoke, 0, 0, TO_ROOM); 
	      }
	    }
	  }
	  
	  // handle choking
	  if(smoke && smoke->getSizeIndex()>=7){
	    for(TThing *t=real_roomp(smoke->in_room)->getStuff();t;t=t->nextThing){
	      TBeing *tb;
	      if(!::number(0,4) && (tb=dynamic_cast<TBeing *>(t))){
		tb->sendTo(COLOR_BASIC, "<r>The large amount of smoke in the room causes you to choke and cough!<1>\n\r");
		rc=tb->reconcileDamage(tb, ::number(3,11), DAMAGE_SUFFOCATION);

		if (IS_SET_DELETE(rc, DELETE_VICT)) {
		  delete tb;
		  continue;
		}

	      }
	    }
	  }
	}	
	if (!pulse_mudhour) {
	  rc = obj->objectTickUpdate(pulse);
          if (IS_SET_DELETE(rc, DELETE_THIS)) {
            next_thing = obj->next;
            delete obj;
            obj = NULL;
	    continue;
          }
        }
        next_thing = obj->next;
      } // object list
    }


    // note on this loop
    // it is possible that temp gets deleted in one of the sub funcs
    // we don't get acknowledgement of this in any way.
    // to avoid problems this might cause, we reinitialize temp at
    // the end (eg, before any deletes, or before we come back around)
    // bottom line is that temp keeps getting set because it might be
    // bogus after the function call.
    for (tmp_ch = character_list; tmp_ch; tmp_ch = temp) {
      temp = tmp_ch->next;  // just for safety

      if (tmp_ch->getPosition() == POSITION_DEAD) {
	vlogf(LOG_BUG, fmt("Error: dead creature (%s at %d) in character_list, removing.") % 
	      tmp_ch->getName() % tmp_ch->in_room);
	delete tmp_ch;
	tmp_ch = NULL;
	continue;
      }
      if ((tmp_ch->getPosition() < POSITION_STUNNED) &&
	  (tmp_ch->getHit() > 0)) {
	vlogf(LOG_BUG, fmt("Error: creature (%s) with hit > 0 found with position < stunned") % 
	      tmp_ch->getName());
	vlogf(LOG_BUG, "Setting player to POSITION_STANDING");
	tmp_ch->setPosition(POSITION_STANDING);
      }

      if (!special_procs) {
	if (tmp_ch->spec) {
	  rc = tmp_ch->checkSpec(tmp_ch, CMD_GENERIC_PULSE, "", NULL);
	  if (IS_SET_DELETE(rc, DELETE_THIS)) {
	    if (!tmp_ch) continue;

	    temp = tmp_ch->next;
	    delete tmp_ch;
	    tmp_ch = NULL;
	    continue;
	  }
	}
      }

      if (!drowning) {
	rc = tmp_ch->checkDrowning();
	if (IS_SET_DELETE(rc, DELETE_THIS)) {
	  temp = tmp_ch->next;
	  delete tmp_ch;
	  tmp_ch = NULL;
	  continue;
	}

	TMonster *tmon = dynamic_cast<TMonster *>(tmp_ch);
	if(tmon){
	  tmon->checkResponses((tmon->opinion.random ? tmon->opinion.random : 
				(tmon->targ() ? tmon->targ() : tmon)),
			       NULL, NULL, CMD_RESP_PULSE);

	}

      }

      if (!mobstuff) {
	if (Gravity) {
	  tmp_ch->checkSinking(tmp_ch->in_room);

	  rc = tmp_ch->checkFalling();
	  if (IS_SET_DELETE(rc, DELETE_THIS)) {
	    temp = tmp_ch->next;
	    delete tmp_ch;
	    tmp_ch = NULL;
	    continue;
	  }
	}
	
	if (!tmp_ch->isPc() && dynamic_cast<TMonster *>(tmp_ch) &&
	    (zone_table[tmp_ch->roomp->getZoneNum()].zone_value!=1 || 
	     tmp_ch->isShopkeeper() || 
	     IS_SET(tmp_ch->specials.act, ACT_HUNTING))){
	  rc = dynamic_cast<TMonster *>(tmp_ch)->mobileActivity(pulse);
	  if (IS_SET_DELETE(rc, DELETE_THIS)) {
	    temp = tmp_ch->next;
	    delete tmp_ch;
	    tmp_ch = NULL;
	    continue;
	  }
	}
	if (tmp_ch->task && (pulse >= tmp_ch->task->nextUpdate)) {
	  TObj *tmper_obj = NULL;
	  if (tmp_ch->task->obj) {
	    tmper_obj = tmp_ch->task->obj; 
	  } 
	  rc = (*(tasks[tmp_ch->task->task].taskf))
	    (tmp_ch, CMD_TASK_CONTINUE, "", pulse, tmp_ch->task->room, tmp_ch->task->obj);
	  if (IS_SET_DELETE(rc, DELETE_ITEM)) {
	    if (tmper_obj) {
	      delete tmper_obj;
	      tmper_obj = NULL;
	    } else {
	      vlogf(LOG_BUG, "bad item delete in gameloop -- task calling");
	    }
	  }
	  if (IS_SET_DELETE(rc, DELETE_THIS)) {
	    temp = tmp_ch->next;
	    delete tmp_ch;
	    tmp_ch = NULL;
	    continue;
	  }
	}
      }

      if (!combat) {

	if (tmp_ch->isPc() && tmp_ch->desc && tmp_ch->GetMaxLevel() > MAX_MORT &&
	    !tmp_ch->limitPowerCheck(CMD_GOTO, tmp_ch->roomp->number)
	    && !tmp_ch->affectedBySpell(SPELL_POLYMORPH) &&
	    !IS_SET(tmp_ch->specials.act, ACT_POLYSELF)) {
	  char tmpbuf[256];
	  strcpy(tmpbuf, "");
	  tmp_ch->sendTo("An incredibly powerful force pulls you back into Imperia.\n\r");
	  act("$n is pulled back whence $e came.", TRUE, tmp_ch, 0, 0, TO_ROOM);
	  vlogf(LOG_BUG,fmt("%s was wandering around the mortal world (R:%d) so moving to office.") % 
		tmp_ch->getName() % tmp_ch->roomp->number);
	    
	  if (!tmp_ch->hasWizPower(POWER_GOTO)) {
	    tmp_ch->setWizPower(POWER_GOTO);
	    tmp_ch->doGoto(tmpbuf);
	    tmp_ch->remWizPower(POWER_GOTO);
	  } else {
	    tmp_ch->doGoto(tmpbuf);
	  }
	  act("$n appears in the room with a sheepish look on $s face.", TRUE, tmp_ch, 0, 0, TO_ROOM);
	}


	if (tmp_ch->spelltask) {
	  rc = (tmp_ch->cast_spell(tmp_ch, CMD_TASK_CONTINUE, pulse));
	  if (IS_SET_DELETE(rc, DELETE_THIS)) {
	    temp = tmp_ch->next;
	    delete tmp_ch;
	    tmp_ch = NULL;
	    continue;
	  }
	}

	rc = tmp_ch->updateAffects();
	if (IS_SET_DELETE(rc, DELETE_THIS)) {
	  // died in update (disease) 
	  temp = tmp_ch->next;
	  delete tmp_ch;
	  tmp_ch = NULL;
	  continue;
    // next line is for the case of doReturn in updateAffects
	} else if (rc == ALREADY_DELETED) continue;

	// this was in hit(), makes more sense here I think
	if (tmp_ch->getMyRace()->hasTalent(TALENT_FAST_REGEN) &&
	    (tmp_ch->getHit() < tmp_ch->hitLimit()) &&
	    !::number(0, 10)){
	  // mostly for trolls
	  act("You regenerate slightly.", TRUE, tmp_ch, 0, 0, TO_CHAR);
	  act("$n regenerates slightly.", TRUE, tmp_ch, 0, 0, TO_ROOM);
	  tmp_ch->addToHit(::number(1,6));
	}

	// soak up attack if not in combat
	if ((tmp_ch->cantHit > 0) && !tmp_ch->fight())
	  tmp_ch->cantHit--;
      }
      if (!teleport) {
	rc = tmp_ch->riverFlow(pulse);
	if (IS_SET_DELETE(rc, DELETE_THIS)) {
	  temp = tmp_ch->next;
	  delete tmp_ch;
	  tmp_ch = NULL;
	  continue;
	}
	rc = tmp_ch->teleportRoomFlow(pulse);
	if (IS_SET_DELETE(rc, DELETE_THIS)) {
	  temp = tmp_ch->next;
	  delete tmp_ch;
	  tmp_ch = NULL;
	  continue;
	}
      }
      TMonster *tmon = dynamic_cast<TMonster *>(tmp_ch);
      if (!update_stuff) {
	if (!number(0, 3) && !tmp_ch->isPc() && tmon)
	  tmon->makeNoise();
      }

      if (!tmp_ch) {
	vlogf(LOG_BUG, "how did we get to here: socket");
	temp = tmp_ch->next;
	continue;
      }


      if (!pulse_tick) {
	rc = tmp_ch->updateHalfTickStuff();
	if (IS_SET_DELETE(rc, DELETE_THIS)) {
	  if (!tmp_ch)
	    continue;

	  temp = tmp_ch->next;
	  delete tmp_ch;
	  tmp_ch = NULL;
	  continue;
        // Next line is for the case of doReturn in halfUpdateTickStuff
	} else if (rc == ALREADY_DELETED)  continue;
      }
      
      if (!pulse_mudhour) {
	rc = tmp_ch->updateTickStuff();
	if (IS_SET_DELETE(rc, DELETE_THIS)) {
	  temp = tmp_ch->next;
	  if (!dynamic_cast<TBeing *>(tmp_ch)) {
	    // something may be corrupting tmp_ch below - bat 8-18-98
	    vlogf(LOG_BUG, "forced crash.  How did we get here?");
	  }
	  delete tmp_ch;
	  tmp_ch = NULL;
	  continue;
	}
      }
	
      // check for vampires in daylight
      if(!quickpulse){
	if(!tmp_ch->roomp->isIndoorSector() && 
	   !tmp_ch->roomp->isRoomFlag(ROOM_INDOORS) &&
	   (tmp_ch->inRoom() != ROOM_VOID) && sunIsUp()){
	    
	  if(tmp_ch->hasQuestBit(TOG_VAMPIRE)){
	    act("<r>Exposure to sunlight causes your skin to ignite!<1>",
		FALSE, tmp_ch, NULL, NULL, TO_CHAR);
	    act("<r>$n's skin ignites in flames as the sunlight shines on $m!<1>",
		FALSE, tmp_ch, NULL, NULL, TO_ROOM);
	      
	    rc=tmp_ch->reconcileDamage(tmp_ch, ::number(20,200), DAMAGE_FIRE);
	      
	    if(IS_SET_DELETE(rc, DELETE_THIS)) {
	      if (!tmp_ch) continue;
		
	      temp = tmp_ch->next;
	      delete tmp_ch;
	      tmp_ch = NULL;
	      continue;
	    }
	  } else if(tmp_ch->hasQuestBit(TOG_BITTEN_BY_VAMPIRE) &&
		    !::number(0,4)){
	    act("Exposure to sunlight makes your skin itch.",
		FALSE, tmp_ch, NULL, NULL, TO_CHAR);
	  }
	}
      }

      // lycanthrope transformation
#if 1
      if(!quickpulse){
	if(tmp_ch->hasQuestBit(TOG_LYCANTHROPE) &&
	   !tmp_ch->hasQuestBit(TOG_TRANSFORMED_LYCANTHROPE)
           && !tmp_ch->isLinkdead() &&
           
	   moonType() == "full" && !sunIsUp()) {
	  lycanthropeTransform(tmp_ch);
          continue;
	} else if(tmp_ch->hasQuestBit(TOG_TRANSFORMED_LYCANTHROPE)){
	  if(moonType() != "full" || sunIsUp()){
	    tmp_ch->remQuestBit(TOG_TRANSFORMED_LYCANTHROPE);
	    tmp_ch->doReturn("", WEAR_NOWHERE, CMD_RETURN);
            continue;
	  } else if(!tmp_ch->fight() && tmp_ch->roomp && 
		    !tmp_ch->roomp->isRoomFlag(ROOM_PEACEFUL) &&
		    !::number(0,24)){
	    tmp_ch->setCombatMode(ATTACK_BERSERK);
	    tmp_ch->goBerserk(NULL);
	  }
	}
      }
#endif


      if (!quickpulse) {
	if (tmp_ch->spec) {
	  rc = tmp_ch->checkSpec(tmp_ch, CMD_GENERIC_QUICK_PULSE, "", NULL);
	  if (IS_SET_DELETE(rc, DELETE_THIS)) {
	    if (!tmp_ch) continue;
	      
	    temp = tmp_ch->next;
	    delete tmp_ch;
	    tmp_ch = NULL;
	    continue;
	  }
	}
      }

      if (tmp_ch->desc && (tmp_ch->vt100() || tmp_ch->ansi())) {
	time_t t1;
	struct tm *tptr;
	if ((t1 = time((time_t *) 0)) != -1) {
	  tptr = localtime(&t1);
	  if (tptr->tm_min != tmp_ch->desc->last.minute) {
	    tmp_ch->desc->last.minute = tptr->tm_min;
	    if (tmp_ch->ansi()) 
	      tmp_ch->desc->updateScreenAnsi(CHANGED_TIME);
	    else
	      tmp_ch->desc->updateScreenVt100(CHANGED_TIME);
	  }
	}
      }
      temp = tmp_ch->next;
    } // character_list

    if (!(pulse % 2399))
      do_check_mail();

    if (!(pulse % 1199))
      sendAutoTips();

    if (!(pulse %100)){
      int which=(pulse/100)%10;
      
      lag_info.current=lag_info.lagtime[which]=time(0)-lagtime_t;
      lagtime_t=time(0);
      lag_info.lagcount[which]=1;

      lag_info.high = max(lag_info.lagtime[which], lag_info.high);
      lag_info.low = min(lag_info.lagtime[which], lag_info.low);
    }

    if(!(pulse % 600)){
      static FILE *p;
      Descriptor *d;
  
      if(p) pclose(p);

      if(gamePort == PROD_GAMEPORT){
	p=popen("/mud/prod/lib/bin/ping sneezy", "w");
      } else if(gamePort == BUILDER_GAMEPORT){
	p=popen("/mud/prod/lib/bin/ping sneezybuilder", "w");
      } else {
	p=popen("/mud/prod/lib/bin/ping sneezybeta", "w");
      }


      for (d = descriptor_list; d; d = d->next) {
        if (d->host && d->character && d->character->isPlayerAction(PLR_PING)){
	  fprintf(p, "%s\n", d->host);
	}
      }
      fprintf(p, "EOM\n");
      fflush(p);
    }

    if (pulse >= 2400) {
      unsigned int secs = time(0) - ticktime;
      ticktime = time(0);

      if (TestCode6) {
    	vlogf(LOG_MISC, fmt("2400 pulses took %ld seconds.  ONE_SEC=%.3f pulses") %  secs % (2400.0/(float) secs));
      }

      // THIS PUSLE = 0 IS NOT SIMPLY FOR LOGGING PURPOSES.
      // if it gets removed all tasks go into hyper mode. So don't.
      pulse = 0;
    }


    systask->CheckTask();
    tics++;			// tics since last checkpoint signal 
  }
  return TRUE;
}


TSocket *TSocket::newConnection()
{
  struct sockaddr_in isa;
#if defined(LINUX)
  unsigned int i;
#else
  int i;
#endif
  TSocket *s;

  i = sizeof(isa);
  if (getsockname(m_sock, (struct sockaddr *) &isa, &i)) {
    perror("getsockname");
    return NULL;
  }
  s = new TSocket(0);
  if ((s->m_sock = accept(m_sock, (struct sockaddr *) (&isa), &i)) < 0) {
    perror("Accept");
    return NULL;
  }
  s->nonBlock();
  return (s);
}

static const sstring IP_String(sockaddr_in &_a)
{
  char buf[256];
#if (defined SUN)
  sprintf( buf, "%d.%d.%d.%d", 
          _a.sin_addr.S_un.S_un_b.s_b1, 
          _a.sin_addr.S_un.S_un_b.s_b2,
          _a.sin_addr.S_un.S_un_b.s_b3,
          _a.sin_addr.S_un.S_un_b.s_b4);
#else
  int n1, n2, n3, n4; 
  n1 = _a.sin_addr.s_addr >> 24; 
  n2 = (_a.sin_addr.s_addr >> 16) - (n1 * 256); 
  n3 = (_a.sin_addr.s_addr >> 8) - (n1 * 65536) - (n2 * 256); 
  n4 = (_a.sin_addr.s_addr) % 256; 
  sprintf(buf, "%d.%d.%d.%d", n4, n3, n2, n1); 
#endif
  return buf;
}

void sig_alrm(int){return;}

int TSocket::newDescriptor()
{
  int a;
#if defined(LINUX)
  unsigned int size;
#else
  int size;
#endif
  Descriptor *newd;
  struct sockaddr_in saiSock;
  struct hostent *he;
  char temphostaddr[255];
  TSocket *s = NULL;

  if (!(s = gSocket->newConnection())) 
    return 0;

  if ((maxdesc + 1) >= avail_descs) {
    vlogf(LOG_MISC, "Descriptor being dumped due to high load - Bug Batopr");
    s->writeToSocket("Sorry.. The game is full...\n\r");
    s->writeToSocket("Please try again later...\n\r");
    close(s->m_sock);
    delete s;
    return 0;
  } else if (s->m_sock > maxdesc)
    maxdesc = s->m_sock;

  newd = new Descriptor(s);

  size = sizeof(saiSock);
  if (getpeername(s->m_sock, (struct sockaddr *) &saiSock, &size) < 0) {
    perror("getpeername");
    *newd->host = '\0';
  } else {
    // we sometimes hang here, so lets log any suspicious events
    // I _think_ the problem is caused by a site that has changed its DNS
    // entry, but the mud's site has not updated the new list yet.
    signal(SIGALRM, sig_alrm);
    time_t init_time = time(0);
    he = gethostbyaddr((const char *) &saiSock.sin_addr, sizeof(struct in_addr), AF_INET);
    time_t fin_time = time(0);

    if (he) {
      if (he->h_name) 
        strcpy(newd->host, he->h_name);
      else 
        strcpy(newd->host, IP_String(saiSock).c_str());
    } else 
      strcpy(newd->host, IP_String(saiSock).c_str());
    strcpy(temphostaddr, IP_String(saiSock).c_str());

    if (fin_time - init_time >= 10)
      vlogf(LOG_BUG, fmt("DEBUG: gethostbyaddr (1) took %d secs to complete for host %s") % (fin_time-init_time) % temphostaddr);

    if (numberhosts) {
      for (a = 0; a <= numberhosts - 1; a++) {
	if (isdigit(hostlist[a][0])) {
	  if (strstr(temphostaddr, hostlist[a])) {
	    s->writeToSocket("Sorry, your site is banned.\n\r");
	    s->writeToSocket("Questions regarding this may be addressed to: ");
            s->writeToSocket(MUDADMIN_EMAIL);
            s->writeToSocket(".\n\r");
            if (!lockmess.empty())
              s->writeToSocket(lockmess.c_str());

            // descriptor deletion handles socket closing
            delete newd;
	    return 0;
	  }
	} else {
	  if (strcasestr(newd->host, hostlist[a])) {
	    s->writeToSocket("Sorry, your site is banned.\n\r");
	    s->writeToSocket("Questions regarding this may be addressed to: ");
            s->writeToSocket(MUDADMIN_EMAIL);
            s->writeToSocket(".\n\r");
            if (!lockmess.empty())
              s->writeToSocket(lockmess.c_str());

            // descriptor deletion handles socket closing
            delete newd;
	    return 0;
	  }
	}
      }
    }
  }

  if (newd->inputProcessing() < 0) {
    delete newd;
    newd = NULL;
    return 0;
  }

  return 1;
}

int TSocket::writeToSocket(const char *txt)
{
  int sofar, thisround, total;

  total = strlen(txt);
  sofar = 0;

  //txt >> m_sock;
 
  do {
    thisround = write(m_sock, txt + sofar, total - sofar);
    if (thisround < 0) {
      if (errno == EWOULDBLOCK)
	break;

      perror("TSocket::writeToSocket(char *)");
      return (-1);
    }
    sofar += thisround;
  }
  while (sofar < total);
  return 0;
}


void TSocket::closeAllSockets()
{
  vlogf(LOG_MISC, "Closing all sockets.");

  while (descriptor_list)
    delete descriptor_list;

  close(m_sock);
}


void TSocket::nonBlock()
{
  if (fcntl(m_sock, F_SETFL, FNDELAY) == -1) {
    perror("Noblock");
    exit(1);
  }
}

void TSocket::initSocket()
{
  const char *opt = "1";
  char hostname[MAXHOSTNAMELEN];
  struct sockaddr_in sa;
  struct hostent *hp;
  struct linger ld;

#if defined(SUN)
  bzero((char *) &sa, sizeof(struct sockaddr_in));
#else
  memset((char *) &sa, 0, sizeof(sa));
#endif

#if 0
  gethostname(hostname, MAXHOSTNAMELEN);
  if (!(hp = gethostbyname(hostname))) {
#else
  if (!(hp = gethostbyname("localhost"))) {
#endif
    vlogf(LOG_BUG, fmt("failed getting hostname structure.  hostname: %s") %  hostname);
    perror("gethostbyname");
    exit(1);
  }
  sa.sin_family = hp->h_addrtype;
  sa.sin_port = htons(m_port);
  if ((m_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Init-socket");
    exit(1);
  }
  if (setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt)) < 0) {
    perror("setsockopt REUSEADDR");
    exit(1);
  }
  ld.l_linger = 1000;
  ld.l_onoff = 0;
#ifdef OSF
  if (setsockopt(m_sock, SOL_SOCKET, SO_LINGER, &ld, sizeof(ld)) < 0) {
#else
  if (setsockopt(m_sock, SOL_SOCKET, SO_LINGER, (char *) &ld, sizeof(ld)) < 0) {
#endif
    perror("setsockopt LINGER");
    exit(1);
  }
  if (bind(m_sock, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
    perror("bind");
    vlogf(LOG_BUG, fmt("initSocket: bind: errno=%d") %  errno);
    close(m_sock);
    exit(0);
  }
  listen(m_sock, 3);
}

TSocket::TSocket(int p) :
  m_sock(0),
  m_port(p)
{
}

TSocket::~TSocket()
{
}
