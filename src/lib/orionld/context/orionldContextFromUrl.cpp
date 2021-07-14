/*
*
* Copyright 2019 FIWARE Foundation e.V.
*
* This file is part of Orion-LD Context Broker.
*
* Orion-LD Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion-LD Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* orionld at fiware dot org
*
* Author: Ken Zangelin
*/
#include <unistd.h>                                              // usleep
#include <semaphore.h>                                           // sem_init, sem_wait, sem_post

#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/types/OrionldProblemDetails.h"                 // OrionldProblemDetails, orionldProblemDetailsFill
#include "orionld/context/OrionldContext.h"                      // OrionldContext
#include "orionld/context/orionldContextFromBuffer.h"            // orionldContextFromBuffer
#include "orionld/contextCache/orionldContextCacheLookup.h"      // orionldContextCacheLookup
#include "orionld/context/orionldContextDownload.h"              // orionldContextDownload
#include "orionld/contextCache/orionldContextCachePersist.h"     // orionldContextCachePersist
#include "orionld/context/orionldContextFromUrl.h"               // Own interface


typedef struct StringListItem
{
  char                    name[256];
  struct StringListItem*  next;
} StringListItem;

static sem_t            contextDownloadListSem;
static StringListItem*  contextDownloadList = NULL;



// -----------------------------------------------------------------------------
//
// contextDownloadListInit - initiaize the 'context download list'
//
void contextDownloadListInit(void)
{
  sem_init(&contextDownloadListSem, 0, 1);  // 0: shared between threads of the same process. 1: free to be taken
  contextDownloadList = NULL;
}



// -----------------------------------------------------------------------------
//
// contextDownloadListLookup - lookup a URL in the list and report if found or not
//
bool contextDownloadListLookup(const char* url)
{
  StringListItem* itemP = contextDownloadList;

  while (itemP != NULL)
  {
    if (strcmp(itemP->name, url) == 0)
      return true;

    itemP = itemP->next;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// contextDownloadListAdd - add a URL to the list
//
void contextDownloadListAdd(const char* url)
{
  StringListItem* itemP = (StringListItem*) malloc(sizeof(StringListItem));

  strncpy(itemP->name, url, sizeof(itemP->name) - 1);
  itemP->next = contextDownloadList;
  contextDownloadList = itemP;
}



// -----------------------------------------------------------------------------
//
// contextDownloadListRemove - remove a URL from the list
//
void contextDownloadListRemove(const char* url)
{
  StringListItem* iterP = contextDownloadList;
  StringListItem* prevP = NULL;
  StringListItem* itemP = NULL;

  while (iterP != NULL)
  {
    if (strcmp(iterP->name, url) == 0)
    {
      itemP = iterP;
      break;
    }

    prevP = iterP;
    iterP = iterP->next;
  }

  if (itemP == NULL)  // Not found!
    return;

  if (prevP == NULL)  // Found as the first item of the list
  {
    contextDownloadList = itemP->next;
    free(itemP);
  }
  else if (itemP->next == NULL)  // Found as the last item of the list
  {
    prevP->next = NULL;
    free(itemP);
  }
  else  // Found in the middle of the list
  {
    prevP->next = itemP->next;
    free(itemP);
  }
}



// -----------------------------------------------------------------------------
//
// contextDownloadListRelease - release all items in the 'context download cache'
//
// The cache is self-cleaning and this function isn't really necessary - except perhaps
// if the broker is killed while serving requests, e.g. while running tests.
//
// This function is ONLY called from the main exit-function, to avoid leaks for valgrind tests.
//
void contextDownloadListRelease(void)
{
  StringListItem* iterP = contextDownloadList;

  while (iterP != NULL)
  {
    StringListItem* current = iterP;
    iterP = iterP->next;
    free(current);
  }
}



// -----------------------------------------------------------------------------
//
// contextCacheWait -
//
static OrionldContext* contextCacheWait(char* url, OrionldProblemDetails* pdP)
{
  int             sleepTime = 0;
  OrionldContext* contextP;

  while (sleepTime < 3000000)  // 3 secs - 3 million microsecs ... CLI param?
  {
    usleep(20000);  // sleep 20 millisecs ... CLI param?
    contextP = orionldContextCacheLookup(url);
    if (contextP != NULL)
      return contextP;
    sleepTime += 20000;
  }

  // The wait timed out
  pdP->status     = 400;  // Assuming the URL is invalid, this a "400 Bad Request"
  pdP->title      = (char*) "Timeout awaiting other thread to download a context";
  pdP->detail     = (char*) url;

  return NULL;
}



// -----------------------------------------------------------------------------
//
// orionldContextFromUrl -
//
OrionldContext* orionldContextFromUrl(char* url, char* id, OrionldProblemDetails* pdP)
{
  OrionldContext* contextP = orionldContextCacheLookup(url);

  if (contextP != NULL)
    return contextP;

  //
  // Make sure the context isn't already being downloaded
  //
  // Three possibilities:
  // CASE 1. No one is trying to download the context, so:
  //         - mark the URL to be downloading - for the next to know
  //         - download it and add it to the context cache
  //         - remove the mark from step 1
  // CASE 2. No one was trying to download the context, so I tried to take the semaphore.
  //         However, another thread got the semaphore before me and started the download
  //         In this case, I will NOT try to download (somebody else is already doing that).
  //         Instead, I will wait for that download to finish and then lookup the context from the cache
  //
  // CASE 3. Someone was actually downloading the context when I wanted to do the same.
  //         Just like step 2 - I wait for the download to complete and then lookup the context from the cache.
  //
  bool urlDownloading = contextDownloadListLookup(url);
  if (urlDownloading == false)
  {
    //
    // Not there, so, we'll download it
    // First take the 'download semaphore'
    //
    sem_wait(&contextDownloadListSem);

    //
    // OK - got the semaphore - but, did I have to wait?
    // I must look the context up again, to be sure (in the 'contextDownloadList')
    //
    // If the URL is in 'contextDownloadList' then somebody else took the semaphore before me
    // and started downloading.
    //
    urlDownloading = contextDownloadListLookup(url);
    if (urlDownloading == false)
      contextDownloadListAdd(url);  // CASE 1: Mark the URL as being downloading

    sem_post(&contextDownloadListSem);

    if (urlDownloading == true)  // If somebody has taken the semaphore before me and is downloading the context - I'll have to wait
      return contextCacheWait(url, pdP);  // CASE 2 - another thread has downloaded the context

    // CASE 1 - the context will be downloaded
  }
  else
    return contextCacheWait(url, pdP);  // CASE 3 - another thread has downloaded the context

  char* buffer = orionldContextDownload(url, pdP);
  if (buffer == NULL)
  {
    // orionldContextDownload fills in pdP
    LM_W(("Bad Input? (%s: %s)", pdP->title, pdP->detail));
    return NULL;
  }

  contextP = orionldContextFromBuffer(url, OrionldContextDownloaded, id, buffer, pdP);
  if (contextP == NULL)
  {
    //
    // Uncomfortable problem here ...
    // The downloaded @context is erroneous and cannot be used.
    // The response to the request indicates this fact, but, if the client
    // insists on trying to use the context (without fixing it), the broker will download it over and over again.
    // Still, I believe this is the best solution.
    //
    LM_E(("Context Error (%s: %s)", pdP->title, pdP->detail));
    contextDownloadListRemove(url);
    return NULL;  // Parse Error?
  }

  contextP->origin    = OrionldContextDownloaded;
  contextP->createdAt = orionldState.requestTime;
  contextP->usedAt    = orionldState.requestTime;

  // Remove the 'url' from the contextDownloadList and persist it to DB
  sem_wait(&contextDownloadListSem);
  contextDownloadListRemove(url);
  sem_post(&contextDownloadListSem);
  orionldContextCachePersist(contextP);

  return contextP;
}
