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
* Author: Ken Zangelin
*/
#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

#include "rest/ConnectionInfo.h"                                 // ConnectionInfo
#include "rest/HttpStatusCode.h"                                 // SccNotFound
#include "mongoBackend/mongoEntityExists.h"                      // mongoEntityExists
#include "mongoBackend/mongoAttributeExists.h"                   // mongoAttributeExists
#include "mongoBackend/mongoUpdateContext.h"                     // mongoUpdateContext
#include "orionld/common/orionldErrorResponse.h"                 // orionldErrorResponseCreate
#include "orionld/common/OrionldConnection.h"                    // orionldState
#include "orionld/serviceRoutines/orionldPatchAttribute.h"       // Own Interface
#include "orionld/context/orionldUriExpand.h"                    // orionldUriExpand
#include "orionld/common/SCOMPARE.h"                             // SCOMPAREx
#include "orionld/common/orionldAttributeTreat.h"                // orionldAttributeTreat

#include "ngsi/ContextElement.h"                                 // ContextElement



// ----------------------------------------------------------------------------
//
// orionldPatchAttribute -
//
bool orionldPatchAttribute(ConnectionInfo* ciP)
{
  LM_T(LmtServiceRoutine, ("In orionldPatchAttribute"));

  char* entityId = orionldState.wildcard[0];

  if (mongoEntityExists(entityId, orionldState.tenant) == false)
  {
    ciP->httpStatusCode = SccNotFound;
    orionldErrorResponseCreate(OrionldBadRequestData, "Entity does not exist", orionldState.wildcard[0], OrionldDetailsString);
    return false;
  }

  // Is the payload empty?
  if (orionldState.requestTree == NULL)
  {
    ciP->httpStatusCode = SccBadRequest;
    orionldErrorResponseCreate(OrionldBadRequestData, "Payload is missing", NULL, OrionldDetailsString);
    return false;
  }

  // Is the payload not a JSON object?
  if  (orionldState.requestTree->type != KjObject)
  {
    ciP->httpStatusCode = SccBadRequest;
    orionldErrorResponseCreate(OrionldBadRequestData, "Payload not a JSON object", kjValueType(orionldState.requestTree->type), OrionldDetailsString);
    return false;
  }

  // Is the payload an empty object?
  if  (orionldState.requestTree->value.firstChildP == NULL)
  {
    ciP->httpStatusCode = SccBadRequest;
    orionldErrorResponseCreate(OrionldBadRequestData, "Payload is an empty JSON object", NULL, OrionldDetailsString);
    return false;
  }


  //
  // URI Expansion for the attribute name, except if "location", "observationSpace", or "operationSpace"
  //
  char*  attrName;
  
  if (SCOMPARE9(orionldState.wildcard[1],       'l', 'o', 'c', 'a', 't', 'i', 'o', 'n', 0))
    attrName = orionldState.wildcard[1];
  else if (SCOMPARE17(orionldState.wildcard[1], 'o', 'b', 's', 'e', 'r', 'v', 'a', 't', 'i', 'o', 'n', 'S', 'p', 'a', 'c', 'e', 0))
    attrName = orionldState.wildcard[1];
  else if (SCOMPARE15(orionldState.wildcard[1], 'o', 'p', 'e', 'r', 'a', 't', 'i', 'o', 'n', 'S', 'p', 'a', 'c', 'e', 0))
    attrName = orionldState.wildcard[1];
  else
  {
      
    char  attrLongName[256];
    char* details;

    if (orionldUriExpand(orionldState.contextP, orionldState.wildcard[1] , attrLongName, sizeof(attrLongName), &details) == true)
      attrName = attrLongName;
    else
    {
     orionldErrorResponseCreate(OrionldBadRequestData, details, orionldState.wildcard[1] , OrionldDetailsAttribute);
     return false;
    }
  }
   
  LM_T(LmtServiceRoutine, ("jorge-log: nome do atributo: %s", attrName));

  // Make sure the attribute to be patched exists
  if (mongoAttributeExists(entityId, attrName, orionldState.tenant) == false)
  {
    ciP->httpStatusCode = SccNotFound;
    orionldErrorResponseCreate(OrionldBadRequestData, "Attribute does not exist", orionldState.wildcard[1], OrionldDetailsString);
    return false;
  }
  
  //if(SCOMPARE6(orionldState.requestTree->value.firstChildP->name, 'v', 'a', 'l', 'u', 'e', 0))
  //{
    //
    // mongo ...
    // - fill in the UpdateContextRequest from the KjTree
    // - call the mongo routine 'mongoUpdateContext'
    //
    UpdateContextRequest   mongoRequest;
    UpdateContextResponse  mongoResponse;
    ContextElement*        ceP       = new ContextElement();  // FIXME: Any way I can avoid to allocate ?
    EntityId*              entityIdP;
    
    mongoRequest.contextElementVector.push_back(ceP);

    entityIdP     = &mongoRequest.contextElementVector[0]->entityId;
    entityIdP->id = entityId;
    mongoRequest.updateActionType = ActionTypeAppendStrict;

    KjNode*            attrTypeNodeP = NULL;
    ContextAttribute*  caP           = new ContextAttribute();
    


    KjNode* kNodeP = orionldState.requestTree->value.firstChildP;
   
     if (orionldAttributeTreat(ciP, kNodeP, caP, &attrTypeNodeP) == false)
    {
      mongoRequest.release();
      LM_E(("orionldAttributeTreat failed"));
      delete caP;
      return false;
    } 
    
     LM_T(LmtServiceRoutine, ("jorge-log: type do atributo: %s", attrTypeNodeP->value.s));

     // NO URI Expansion for Attribute TYPE
    caP->type = attrTypeNodeP->value.s;

    // Add the attribute to the attr vector
    ceP->contextAttributeVector.push_back(caP);

     //
    // Call mongoBackend - FIXME: call postUpdateContext, not mongoUpdateContext
    //
     ciP->httpStatusCode = mongoUpdateContext(&mongoRequest,
                                           &mongoResponse,
                                           orionldState.tenant,
                                           ciP->servicePathV,
                                           ciP->uriParam,
                                           ciP->httpHeaders.xauthToken,
                                           ciP->httpHeaders.correlator,
                                           ciP->httpHeaders.ngsiv2AttrsFormat,
                                           ciP->apiVersion,
                                           NGSIV2_NO_FLAVOUR);
    if (ciP->httpStatusCode == SccOk)
      ciP->httpStatusCode = SccNoContent;
    else
    {
      LM_E(("mongoUpdateContext: HTTP Status Code: %d", ciP->httpStatusCode));
      orionldErrorResponseCreate(OrionldBadRequestData, "Internal Error", "Error from Mongo-DB backend", OrionldDetailsString);
      return false;
    }

  /* }
  else
  {
   ciP->httpStatusCode = SccBadRequest;
   orionldErrorResponseCreate(OrionldBadRequestData, "Invalid content in the payload.", orionldState.requestTree->value.firstChildP->name , OrionldDetailsString);

   return false;
  } */
  
  //ciP->httpStatusCode = SccNotImplemented;
  //orionldErrorResponseCreate(OrionldBadRequestData, "Not implemented - PATCH /ngsi-ld/v1/entities/*/attrs", orionldState.wildcard[0], OrionldDetailsString);
  
   LM_T(LmtServiceRoutine, ("jorge-log: valor do atributo: %s", orionldState.requestTree->value.firstChildP->value));
  
  return true;
}
