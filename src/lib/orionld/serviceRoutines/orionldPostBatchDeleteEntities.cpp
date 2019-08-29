/*
*
* Copyright 2018 Telefonica Investigacion y Desarrollo, S.A.U
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
#include "orionld/serviceRoutines/orionldPostBatchDeleteEntities.h"   // Own interface



// ----------------------------------------------------------------------------
//
// orionldPostBatchDeleteEntities -
//
bool orionldPostBatchDeleteEntities(ConnectionInfo* ciP)
{
  LM_TMP(("In orionldPostBatchDeleteEntities"));

  // KjNode* entityIds = orionldState.requestTree->value;


  // TESTING THE SERVICE CALL FIRST
  orionldErrorResponseCreate(OrionldBadRequestData, "Not implemented - POST /ngsi-ld/v1/entityOperations/delete", NULL, OrionldDetailsString);

  ciP->httpStatusCode = SccNotImplemented;
  
  return true;
}
