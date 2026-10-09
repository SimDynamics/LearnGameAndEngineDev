package com.learngraphicsprogramming.app

import android.content.res.ColorStateList
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ProgressBar
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView

class LessonAdapter(

    private val lessons: List<Lesson>,

    private val routeId: String =
        AppearanceSettingsRepository.ROUTE_PROGRAMMING,

    private val onLessonClick: (Lesson) -> Unit

) : RecyclerView.Adapter<LessonAdapter.LessonViewHolder>() {

    class LessonViewHolder(
        itemView: View
    ) : RecyclerView.ViewHolder(
        itemView
    ) {

        val textLessonNumber: TextView =
            itemView.findViewById(
                R.id.textLessonNumber
            )

        val textLessonTitle: TextView =
            itemView.findViewById(
                R.id.textLessonTitle
            )

        val textLessonProgress: TextView =
            itemView.findViewById(
                R.id.textLessonProgress
            )

        val textLessonPercentage: TextView =
            itemView.findViewById(
                R.id.textLessonPercentage
            )

        val progressLesson: ProgressBar =
            itemView.findViewById(
                R.id.progressLesson
            )
    }

    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): LessonViewHolder {

        val view =
            LayoutInflater
                .from(
                    parent.context
                )
                .inflate(
                    R.layout.item_lesson,
                    parent,
                    false
                )

        return LessonViewHolder(
            view
        )
    }

    override fun onBindViewHolder(
        holder: LessonViewHolder,
        position: Int
    ) {

        val lesson =
            lessons[position]

        val context =
            holder.itemView.context

        val accentColor =
            AppearanceSettingsRepository
                .getRouteAccentColor(
                    context,
                    routeId
                )

        val accentTextColor =
            AppearanceSettingsRepository
                .getRouteAccentTextColor(
                    context,
                    routeId
                )

        holder.textLessonNumber.text =
            context.getString(
                R.string.nav_lesson_number,
                lesson.number
            )

        holder.textLessonNumber
            .setTextColor(
                accentTextColor
            )

        holder.textLessonTitle.text =
            lesson.title

        val progress =
            ProgressRepository
                .getLessonPageProgress(
                    context,
                    routeId,
                    lesson.id
                )

        holder.textLessonProgress.text =
            context.resources.getQuantityString(
                R.plurals.lesson_completed_pages,
                progress.total,
                progress.completed,
                progress.total
            )

        holder.textLessonPercentage.text =
            context.getString(R.string.lesson_percentage, progress.percentage)

        holder.progressLesson.max =
            100

        holder.progressLesson.progress =
            progress.percentage

        holder.progressLesson.progressTintList =
            ColorStateList.valueOf(
                accentColor
            )

        holder.itemView.setOnClickListener {

            onLessonClick(
                lesson
            )
        }
    }

    override fun getItemCount(): Int {

        return lessons.size
    }
}
