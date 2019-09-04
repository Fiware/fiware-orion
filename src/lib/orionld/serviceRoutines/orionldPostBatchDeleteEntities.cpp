/*
*
* Copyright 2019 Telefonica Investigacion y Desarrollo, S.A.U
*
* This file is part of Orion Context Broker.
*
* Orion Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* iot_support at tid dot es
*
* Author: Larysse Savanna
*/
#include "logMsg/logMsg.h"                                            // LM_*
#include "logMsg/traceLevels.h"                                       // Lmt*

extern "C"
{
#include "kjson/KjNode.h"                                             // KjNode
#include "kjson/kjBuilder.h"                                          // kjString, kjObject, ...
#include "kjson/kjRender.h"                                           // kjRender
}

#include "logMsg/logMsg.h"                                            // LM_*
#include "logMsg/traceLevels.h"                                       // Lmt*

#include "rest/ConnectionInfo.h"                                      // ConnectionInfo
#include "orionld/common/orionldState.h"                              // orionldState
#include "orionld/db/dbEntityBatchDelete.h"                           // dbEntityBatchDelete.h
#include "orionld/common/orionldErrorResponse.h"                      // orionldErrorResponseCreate
#include "ngsi10/UpdateContextRequest.h"                              // UpdateContextRequest
#include "ngsi10/UpdateContextResponse.h"                             // UpdateContextResponse
#include "orionld/mongoCppLegacy/mongoCppLegacyEntityBatchDelete.h"   // mongoCppLegacyEntityBatchDelete
#include "orionld/serviceRoutines/orionldPostBatchDeleteEntities.h"   // Own interface



// ----------------------------------------------------------------------------
//
// orionldPostBatchDeleteEntities -
//
bool orionldPostBatchDeleteEntities(ConnectionInfo* ciP)
{
  
  LM_TMP(("LARYSSE: Payload is a JSON %s", kjValueType(orionldState.requestTree->type)));

  if (orionldState.requestTree->type != KjArray)
  {
    orionldErrorResponseCreate(OrionldBadRequestData, "Invalid payload", "must be a JSON array", OrionldDetailsString);
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  //
  // Debugging - see all IDs
  //
  int ix = 0;
  for (KjNode* idNodeP = orionldState.requestTree->value.firstChildP; idNodeP != NULL; idNodeP = idNodeP->next)
  {
    if (idNodeP->type != KjString)
    {
      orionldErrorResponseCreate(OrionldBadRequestData, "Invalid payload", "must be a JSON Array of JSON Strings", OrionldDetailsString);
      ciP->httpStatusCode	= SccBadRequest;
      return false;
    }

    ++ix;
  }

  mongoCppLegacyEntityBatchDelete(orionldState.requestTree);
  
  ciP->httpStatusCode = SccOk;  // 200 ok

  return true;
}
