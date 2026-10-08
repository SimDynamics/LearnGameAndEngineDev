package com.learngraphicsprogramming.app

import android.content.res.ColorStateList
import android.graphics.Color
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.RecyclerView
import com.learngraphicsprogramming.app.databinding.ItemCourseBinding

class CourseAdapter(

    private val courses: List<Course>,

    private val routeId: String =
        AppearanceSettingsRepository.ROUTE_PROGRAMMING,

    private val onCourseClick: (Course) -> Unit

) : RecyclerView.Adapter<CourseAdapter.CourseViewHolder>() {

    class CourseViewHolder(
        val binding: ItemCourseBinding
    ) : RecyclerView.ViewHolder(
        binding.root
    )

    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): CourseViewHolder {

        val binding =
            ItemCourseBinding.inflate(
                LayoutInflater.from(
                    parent.context
                ),
                parent,
                false
            )

        return CourseViewHolder(
            binding
        )
    }

    override fun onBindViewHolder(
        holder: CourseViewHolder,
        position: Int
    ) {

        val course =
            courses[position]

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

        val progress =
            ProgressRepository
                .getCourseProgress(
                    context,
                    routeId,
                    course.id
                )

        holder.binding.progressCourse.progressTintList =
            ColorStateList.valueOf(
                accentColor
            )

        /*
         * INFORMACIÓN DEL CURSO.
         *
         * Utilizamos los recursos traducidos
         * sin modificar los datos de progreso.
         */
        holder.binding.textCourseNumber.text =
            context.getString(
                R.string.nav_course_number,
                course.number,
                courses.size
            )

        holder.binding.textCourseTitle.text =
            course.title

        if (
            progress.total == 0
        ) {

            holder.binding.textCourseProgress.text =
                context.getString(
                    R.string.nav_coming_soon
                )

            holder.binding.textCoursePercentage.text =
                ""

            holder.binding.progressCourse.progress =
                0

        } else {

            holder.binding.textCourseProgress.text =
                context.getString(
                    R.string.nav_completed_lessons,
                    progress.completed,
                    progress.total
                )

            holder.binding.textCoursePercentage.text =
                "${progress.percentage} %"

            holder.binding.progressCourse.progress =
                progress.percentage
        }

        if (
            progress.total > 0 &&
            progress.completed ==
            progress.total
        ) {

            holder.binding.courseContainer
                .setBackgroundColor(
                    Color.rgb(
                        29,
                        78,
                        52
                    )
                )

            holder.binding.textCourseNumber
                .setTextColor(
                    Color.rgb(
                        105,
                        220,
                        145
                    )
                )

        } else {

            holder.binding.courseContainer
                .setBackgroundColor(
                    Color.rgb(
                        32,
                        33,
                        38
                    )
                )

            holder.binding.textCourseNumber
                .setTextColor(
                    accentTextColor
                )
        }

        holder.binding.courseContainer
            .setOnClickListener {

                onCourseClick(
                    course
                )
            }
    }

    override fun getItemCount(): Int {

        return courses.size
    }
}