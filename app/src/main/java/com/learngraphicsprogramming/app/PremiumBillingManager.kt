package com.learngraphicsprogramming.app

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.widget.Toast
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

                when (
                    billingResult.responseCode
                ) {

                    BillingClient.BillingResponseCode.OK -> {

                        /*
                         * Detectamos pagos pendientes.
                         * Todavía no activan Premium.
                         */
                        val pendingPremium =
                            purchases.orEmpty().any {
                                    purchase ->

                                purchase.products.contains(
                                    PREMIUM_PRODUCT_ID
                                ) &&
                                        purchase.purchaseState ==
                                        Purchase.PurchaseState.PENDING
                            }

                        if (
                            pendingPremium
                        ) {

                            Handler(
                                Looper.getMainLooper()
                            ).post {

                                Toast.makeText(
                                    context.applicationContext,
                                    "El pago está pendiente. Premium se activará cuando Google Play lo confirme.",
                                    Toast.LENGTH_LONG
                                ).show()
                            }
                        }

                        /*
                         * Consultamos las compras reales
                         * antes de actualizar Premium.
                         */
                        refreshPurchases(
                            context.applicationContext
                        )
                    }

                    BillingClient.BillingResponseCode.USER_CANCELED -> {

                        /*
                         * El usuario canceló la compra.
                         * No modificamos su estado Premium.
                         */
                    }

                    BillingClient.BillingResponseCode.ITEM_ALREADY_OWNED -> {

                        /*
                         * Google Play indica que el usuario
                         * ya posee el producto.
                         */
                        refreshPurchases(
                            context.applicationContext
                        )
                    }

                    else -> {

                        Handler(
                            Looper.getMainLooper()
                        ).post {

                            Toast.makeText(
                                context.applicationContext,
                                "No se pudo completar la operación en Google Play.",
                                Toast.LENGTH_LONG
                            ).show()
                        }
                    }
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
                activity.applicationContext
            )

            Toast.makeText(
                activity,
                "Google Play todavía no está disponible. Inténtalo nuevamente en un momento.",
                Toast.LENGTH_LONG
            ).show()

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

            /*
             * Google Play no pudo consultar
             * los detalles del producto.
             */
            if (
                billingResult.responseCode !=
                BillingClient.BillingResponseCode.OK
            ) {

                activity.runOnUiThread {

                    Toast.makeText(
                        activity,
                        "No se pudo consultar Premium en Google Play. Inténtalo nuevamente.",
                        Toast.LENGTH_LONG
                    ).show()
                }

                return@queryProductDetailsAsync
            }

            /*
             * La consulta funcionó, pero
             * premium_lifetime no está disponible.
             */
            if (
                queryResult.productDetailsList.none {
                    it.productId == PREMIUM_PRODUCT_ID
                }
            ) {

                activity.runOnUiThread {

                    Toast.makeText(
                        activity,
                        "Premium todavía no está disponible en Google Play.",
                        Toast.LENGTH_LONG
                    ).show()
                }

                return@queryProductDetailsAsync
            }

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

                            /*
                             * Intentamos abrir la pantalla
                             * de compra de Google Play.
                             *
                             * Si Google Play rechaza iniciar
                             * la operación, informamos
                             * al usuario.
                             */
                            val launchResult =
                                client.launchBillingFlow(
                                    activity,
                                    billingFlowParams
                                )

                            if (
                                launchResult.responseCode !=
                                BillingClient.BillingResponseCode.OK
                            ) {

                                Toast.makeText(
                                    activity,
                                    "No se pudo iniciar la compra Premium. Inténtalo nuevamente.",
                                    Toast.LENGTH_LONG
                                ).show()
                            }
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