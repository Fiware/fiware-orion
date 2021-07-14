/*
*
* Copyright 2020 FIWARE Foundation e.V.
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
#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "orionld/common/pqHeader.h"                           // Postgres header
#include "orionld/troe/pgTransactionCommit.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// pgTransactionCommit - commit a transaction
//
bool pgTransactionCommit(PGconn* connectionP)
{
  PGresult* res;

  res = PQexec(connectionP, "COMMIT");
  if (res == NULL)
    LM_RE(false, ("Database Error (PQexec(COMMIT): %s)", PQresStatus(PQresultStatus(res))));
  PQclear(res);

  if (PQstatus(connectionP) != CONNECTION_OK)
    LM_E(("Database Error (SQL: bad connection: %d)", PQstatus(connectionP)));  // FIXME: string! (last error?)

  PGTransactionStatusType st;
  if ((st = PQtransactionStatus(connectionP)) != PQTRANS_IDLE)
    LM_E(("Database Error (SQL transaction error: %d)", st));  // FIXME: string! (last error?)

  char* errorMsg = PQerrorMessage(connectionP);
  if ((errorMsg != NULL) && (errorMsg[0] != 0))
    LM_E(("Database Error (SQL Commit Error: %s)", errorMsg));

  return true;
}
