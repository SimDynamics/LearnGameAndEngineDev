package com.learngraphicsprogramming.app

import android.content.Context
import com.android.billingclient.api.BillingClient
import com.android.billingclient.api.BillingClientStateListener
import com.android.billingclient.api.BillingResult
import com.android.billingclient.api.PendingPurchasesParams
import com.android.billingclient.api.Purchase
import com.android.billingclient.api.PurchasesUpdatedListener
import com.android.billingclient.api.QueryPurchasesParams

object PremiumBillingManager {

    /*
     * ID que tendrá la compra permanente
     * dentro de Google Play Console.
     */
    const val PREMIUM_PRODUCT_ID =
        "premium_lifetime"

    private var billingClient:
            BillingClient? =
        null

    fun connect(
        context: Context
    ) {

        val existingClient =
            billingClient

        if (
            existingClient != null &&
            existingClient.isReady
        ) {

            refreshPurchases(
                context
            )

            return
        }

        val purchasesUpdatedListener =
            PurchasesUpdatedListener {
                    billingResult,
                    purchases ->

                if (
                    billingResult.responseCode ==
                    BillingClient.BillingResponseCode.OK
                ) {

                    updatePremiumState(
                        context,
                        purchases
                            ?: emptyList()
                    )
                }
            }

        val client =
            BillingClient.newBuilder(
                context.applicationContext
            )
                .setListener(
                    purchasesUpdatedListener
                )
                .enablePendingPurchases(
                    PendingPurchasesParams
                        .newBuilder()
                        .enableOneTimeProducts()
                        .build()
                )
                .enableAutoServiceReconnection()
                .build()

        billingClient =
            client

        client.startConnection(
            object : BillingClientStateListener {

                override fun onBillingSetupFinished(
                    billingResult: BillingResult
                ) {

                    if (
                        billingResult.responseCode ==
                        BillingClient.BillingResponseCode.OK
                    ) {

                        refreshPurchases(
                            context
                        )
                    }
                }

                override fun onBillingServiceDisconnected() {

                    /*
                     * enableAutoServiceReconnection()
                     * se encarga de reconectar cuando
                     * sea necesario.
                     */
                }
            }
        )
    }

    private fun refreshPurchases(
        context: Context
    ) {

        val client =
            billingClient
                ?: return

        val params =
            QueryPurchasesParams
                .newBuilder()
                .setProductType(
                    BillingClient.ProductType.INAPP
                )
                .build()

        client.queryPurchasesAsync(
            params
        ) {
                billingResult,
                purchases ->

            if (
                billingResult.responseCode ==
                BillingClient.BillingResponseCode.OK
            ) {

                updatePremiumState(
                    context,
                    purchases
                )
            }
        }
    }

    private fun updatePremiumState(
        context: Context,
        purchases: List<Purchase>
    ) {

        val premiumPurchased =
            purchases.any {
                    purchase ->

                purchase.products.contains(
                    PREMIUM_PRODUCT_ID
                ) &&
                        purchase.purchaseState ==
                        Purchase.PurchaseState.PURCHASED
            }

        if (
            premiumPurchased
        ) {

            PremiumRepository.setVerifiedPremium(
                context
            )

        } else {

            PremiumRepository.setVerifiedFree(
                context
            )
        }
    }
}