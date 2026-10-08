package com.learngraphicsprogramming.app

import android.app.Activity
import android.content.Context
import com.android.billingclient.api.AcknowledgePurchaseParams
import com.android.billingclient.api.BillingClient
import com.android.billingclient.api.BillingClientStateListener
import com.android.billingclient.api.BillingFlowParams
import com.android.billingclient.api.BillingResult
import com.android.billingclient.api.PendingPurchasesParams
import com.android.billingclient.api.Purchase
import com.android.billingclient.api.PurchasesUpdatedListener
import com.android.billingclient.api.QueryProductDetailsParams
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

                    processPurchases(
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

    fun launchPremiumPurchase(
        activity: Activity
    ) {

        val client =
            billingClient

        if (
            client == null ||
            !client.isReady
        ) {

            connect(
                activity
            )

            return
        }

        /*
         * Consultamos los datos actuales del producto
         * justo antes de comprarlo.
         *
         * No conservamos ProductDetails de forma
         * permanente porque Google recomienda
         * utilizar información reciente.
         */
        val product =
            QueryProductDetailsParams.Product
                .newBuilder()
                .setProductId(
                    PREMIUM_PRODUCT_ID
                )
                .setProductType(
                    BillingClient.ProductType.INAPP
                )
                .build()

        val params =
            QueryProductDetailsParams
                .newBuilder()
                .setProductList(
                    listOf(
                        product
                    )
                )
                .build()

        client.queryProductDetailsAsync(
            params
        ) {
                billingResult,
                queryResult ->

            if (
                billingResult.responseCode ==
                BillingClient.BillingResponseCode.OK
            ) {

                val productDetails =
                    queryResult
                        .productDetailsList
                        .firstOrNull {
                                details ->

                            details.productId ==
                                    PREMIUM_PRODUCT_ID
                        }

                if (
                    productDetails != null
                ) {

                    /*
                     * Premium tendrá una sola opción
                     * de compra permanente.
                     */
                    val offerDetails =
                        productDetails
                            .oneTimePurchaseOfferDetailsList
                            ?.firstOrNull()
                            ?: productDetails.oneTimePurchaseOfferDetails

                    if (
                        offerDetails != null
                    ) {

                        activity.runOnUiThread {

                            val productDetailsParamsBuilder =
                                BillingFlowParams
                                    .ProductDetailsParams
                                    .newBuilder()
                                    .setProductDetails(
                                        productDetails
                                    )

                            val offerToken =
                                offerDetails.offerToken

                            if (
                                !offerToken.isNullOrBlank()
                            ) {

                                productDetailsParamsBuilder
                                    .setOfferToken(
                                        offerToken
                                    )
                            }

                            val productDetailsParams =
                                productDetailsParamsBuilder.build()

                            val billingFlowParams =
                                BillingFlowParams
                                    .newBuilder()
                                    .setProductDetailsParamsList(
                                        listOf(
                                            productDetailsParams
                                        )
                                    )
                                    .build()

                            client.launchBillingFlow(
                                activity,
                                billingFlowParams
                            )
                        }
                    }
                }
            }
        }
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

                processPurchases(
                    context,
                    purchases
                )
            }
        }
    }

    private fun processPurchases(
        context: Context,
        purchases: List<Purchase>
    ) {

        val premiumPurchase =
            purchases
                .firstOrNull {
                        purchase ->

                    purchase.products.contains(
                        PREMIUM_PRODUCT_ID
                    ) &&
                            purchase.purchaseState ==
                            Purchase.PurchaseState.PURCHASED
                }

        if (
            premiumPurchase == null
        ) {

            PremiumRepository.setVerifiedFree(
                context
            )

            return
        }

        /*
         * La compra existe y Google Play
         * ya la considera completada.
         */
        PremiumRepository.setVerifiedPremium(
            context
        )

        /*
         * Las compras permanentes no consumibles
         * deben confirmarse.
         *
         * Si ya fue confirmada anteriormente,
         * no hacemos nada.
         */
        if (
            !premiumPurchase.isAcknowledged
        ) {

            acknowledgePurchase(
                premiumPurchase
            )
        }
    }

    private fun acknowledgePurchase(
        purchase: Purchase
    ) {

        val client =
            billingClient
                ?: return

        val params =
            AcknowledgePurchaseParams
                .newBuilder()
                .setPurchaseToken(
                    purchase.purchaseToken
                )
                .build()

        client.acknowledgePurchase(
            params
        ) {
                billingResult ->

            /*
             * Si la confirmación falla temporalmente,
             * la próxima consulta de compras volverá
             * a encontrarla sin confirmar e intentará
             * procesarla nuevamente.
             */
            if (
                billingResult.responseCode !=
                BillingClient.BillingResponseCode.OK
            ) {

                return@acknowledgePurchase
            }
        }
    }
}