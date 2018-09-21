// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ChangeTrustOpFrame.h"
#include "database/Database.h"
#include "ledger/LedgerManager.h"
#include "ledger/TrustFrame.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

ChangeTrustOpFrame::ChangeTrustOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mChangeTrust(mOperation.body.changeTrustOp())
{
}
bool
ChangeTrustOpFrame::doApply(Application& app, LedgerDelta& delta,
                            LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();

    auto tlI = TrustFrame::loadTrustLineIssuer(getSourceID(), mChangeTrust.line,
                                               db, delta);

    auto& mTrustLine = tlI.first;
    auto& issuer = tlI.second;

    if (app.getLedgerManager().getCurrentLedgerVersion() > 2)
    {
        if (issuer && (issuer->getID() == getSourceID()))
        { // since version 3 it is
            // not allowed to use
            // CHANGE_TRUST on self
            app.getMetrics()
                .NewMeter({"op-change-trust", "failure", "trust-self"},
                          "operation")
                .Mark();
            innerResult().code(CHANGE_TRUST_SELF_NOT_ALLOWED);
            return false;
        }
    }

    if (mTrustLine)
    { // we are modifying an old trustline

        if (mChangeTrust.limit < mTrustLine->getBalance())
        { // Can't drop the limit
            // below the balance you
            // are holding with them
            app.getMetrics()
                .NewMeter({"op-change-trust", "failure", "invalid-limit"},
                          "operation")
                .Mark();
            innerResult().code(CHANGE_TRUST_INVALID_LIMIT);
            return false;
        }

        if (mChangeTrust.limit == 0)
        {
            // line gets deleted
            mTrustLine->storeDelete(delta, db);
            mSourceAccount->addNumEntries(-1, ledgerManager);
            mSourceAccount->storeChange(delta, db);
        }
        else
        {
            if (!issuer)
            {
                app.getMetrics()
                    .NewMeter({"op-change-trust", "failure", "no-issuer"},
                              "operation")
                    .Mark();
                innerResult().code(CHANGE_TRUST_NO_ISSUER);
                return false;
            }
            mTrustLine->getTrustLine().limit = mChangeTrust.limit;
            mTrustLine->storeChange(delta, db);
        }
        app.getMetrics()
            .NewMeter({"op-change-trust", "success", "apply"}, "operation")
            .Mark();
        innerResult().code(CHANGE_TRUST_SUCCESS);
        return true;
    }
    else
    { // new trust line
        if (mChangeTrust.limit == 0)
        {
            app.getMetrics()
                .NewMeter({"op-change-trust", "failure", "invalid-limit"},
                          "operation")
                .Mark();
            innerResult().code(CHANGE_TRUST_INVALID_LIMIT);
            return false;
        }
        if (!issuer)
        {
            app.getMetrics()
                .NewMeter({"op-change-trust", "failure", "no-issuer"},
                          "operation")
                .Mark();
            innerResult().code(CHANGE_TRUST_NO_ISSUER);
            return false;
        }
        mTrustLine = std::make_shared<TrustFrame>();
        auto& tl = mTrustLine->getTrustLine();
        tl.accountID = getSourceID();
        tl.asset = mChangeTrust.line;
        tl.limit = mChangeTrust.limit;
        tl.balance = 0;
        mTrustLine->setAuthorized(!issuer->isAuthRequired());

        if (!mSourceAccount->addNumEntries(1, ledgerManager))
        {
            app.getMetrics()
                .NewMeter({"op-change-trust", "failure", "low-reserve"},
                          "operation")
                .Mark();
            innerResult().code(CHANGE_TRUST_LOW_RESERVE);
            return false;
        }

        mSourceAccount->storeChange(delta, db);
        mTrustLine->storeAdd(delta, db);

        app.getMetrics()
            .NewMeter({"op-change-trust", "success", "apply"}, "operation")
            .Mark();
        innerResult().code(CHANGE_TRUST_SUCCESS);
        return true;
    }
}

bool
ChangeTrustOpFrame::doCheckValid(Application& app)
{
    if (mChangeTrust.limit < 0)
    {
        app.getMetrics()
            .NewMeter(
                {"op-change-trust", "invalid", "malformed-negative-limit"},
                "operation")
            .Mark();
        innerResult().code(CHANGE_TRUST_MALFORMED);
        return false;
    }
    if (!isAssetValid(mChangeTrust.line))
    {
        app.getMetrics()
            .NewMeter({"op-change-trust", "invalid", "malformed-invalid-asset"},
                      "operation")
            .Mark();
        innerResult().code(CHANGE_TRUST_MALFORMED);
        return false;
    }
    if (app.getLedgerManager().getCurrentLedgerVersion() > 9)
    {
        if (mChangeTrust.line.type() == ASSET_TYPE_NATIVE)
        {
            app.getMetrics()
                .NewMeter(
                    {"op-change-trust", "invalid", "malformed-native-asset"},
                    "operation")
                .Mark();
            innerResult().code(CHANGE_TRUST_MALFORMED);
            return false;
        }
    }
    return true;
}
}
