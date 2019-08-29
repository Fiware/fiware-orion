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
#include "mongo/client/dbclient.h"                               // MongoDB C++ Client Legacy Driver

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjRender.h"                                      // kjRender - TMP
}

#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

#include "mongoBackend/MongoGlobal.h"                            // getMongoConnection, releaseMongoConnection, ...
#include "orionld/common/orionldState.h"                         // orionldState, dbName, mongoEntitiesCollectionP
#include "orionld/db/dbCollectionPathGet.h"                      // dbCollectionPathGet
#include "orionld/db/dbConfiguration.h"                          // dbDataToKjTree, dbDataFromKjTree
#include "orionld/mongoCppLegacy/mongoCppLegacyEntityDelete.h"   // Own interface



// -----------------------------------------------------------------------------
//
// mongoCppLegacyEntityBatchDelete -
//
bool mongoCppLegacyEntityBatchDelete(const std::vector<std::string>* entityId)
{
  // Start implementation
  char                   collectionPath[256];

  dbCollectionPathGet(collectionPath, sizeof(collectionPath), "entities");
  LM_TMP(("DB: Collection Path: %s", collectionPath));

  //
  // Populate filter - only Entity ID for this operation
  //

  // Testing with only one static id first
  mongo::BSONObjBuilder  filter;
  filter.append("_id.id", "http://a.b.c/entity/idTest");

  mongo::DBClientBase*  connectionP = getMongoConnection();
  mongo::Query          query(filter.obj());
  connectionP->remove(collectionPath, query, 0);
  releaseMongoConnection(connectionP);

  return true;
}
