package com.learngraphicsprogramming.app

import android.app.AlertDialog
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class UnitOptionsActivity : AppCompatActivity() {

    override fun onCreate(
        savedInstanceState: Bundle?
    ) {

        super.onCreate(
            savedInstanceState
        )

        setContentView(
            R.layout.activity_unit_options
        )

        val routeId =
            intent.getStringExtra(
                "routeId"
            ) ?: AppearanceSettingsRepository.ROUTE_PROGRAMMING

        val courseId =
            intent.getStringExtra(
                "courseId"
            ) ?: return

        val unitId =
            intent.getStringExtra(
                "unitId"
            ) ?: return

        val unitNumber =
            intent.getIntExtra(
                "unitNumber",
                1
            )

        val unitTitle =
            intent.getStringExtra(
                "unitTitle"
            ) ?: ""

        val buttonBack =
            findViewById<Button>(
                R.id.buttonBack
            )

        val textUnitNumber =
            findViewById<TextView>(
                R.id.textUnitNumber
            )

        val textUnitTitle =
            findViewById<TextView>(
                R.id.textUnitTitle
            )

        val buttonResetUnit =
            findViewById<Button>(
                R.id.buttonResetUnit
            )

        textUnitNumber.text =
            getString(R.string.nav_unit_number, unitNumber)

        textUnitNumber.setTextColor(
            AppearanceSettingsRepository
                .getRouteAccentTextColor(
                    this,
                    routeId
                )
        )

        textUnitTitle.text =
            unitTitle

        buttonBack.setOnClickListener {

            finish()
        }

        buttonResetUnit.setOnClickListener {

            AlertDialog.Builder(
                this
            )
                .setTitle(
                    getString(R.string.unit_settings_reset)
                )
                .setMessage(
                    getString(R.string.unit_settings_reset_confirmation)
                )
                .setNegativeButton(
                    getString(R.string.settings_cancel),
                    null
                )
                .setPositiveButton(
                    getString(R.string.settings_reset)
                ) { _, _ ->

                    ProgressRepository.resetUnit(
                        this,
                        routeId,
                        courseId,
                        unitId
                    )

                    Toast.makeText(
                        this,
                        getString(R.string.unit_settings_reset_success),
                        Toast.LENGTH_SHORT
                    ).show()

                    finish()
                }
                .show()
        }
    }

    override fun onResume() {

        super.onResume()

        val routeId =
            intent.getStringExtra(
                "routeId"
            ) ?: AppearanceSettingsRepository.ROUTE_PROGRAMMING

        AppearanceUi.applyRoute(
            this,
            routeId
        )
    }
}